/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * CMSPAR, some architectures can't have space and mark parity.
 */

#ifndef CMSPAR
#define CMSPAR			0
#endif

/*
 * Major and minor numbers.
 */

#define XR_USB_SERIAL_TTY_MAJOR		    266
#define XR_USB_SERIAL_TTY_MINORS		32

/*
 * Requests.
 */

#define USB_RT_XR_USB_SERIAL		(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

/*
 * Output control lines.
 */

#define XR_USB_SERIAL_CTRL_DTR		0x01
#define XR_USB_SERIAL_CTRL_RTS		0x02

/*
 * Input control lines and line errors.
 */

#define XR_USB_SERIAL_CTRL_DCD		0x01
#define XR_USB_SERIAL_CTRL_DSR		0x02
#define XR_USB_SERIAL_CTRL_BRK		0x04
#define XR_USB_SERIAL_CTRL_RI		0x08

#define XR_USB_SERIAL_CTRL_FRAMING	0x10
#define XR_USB_SERIAL_CTRL_PARITY		0x20
#define XR_USB_SERIAL_CTRL_OVERRUN	0x40

/*
 * Internal driver structures.
 */

/*
 * The only reason to have several buffers is to accommodate assumptions
 * in line disciplines. They ask for empty space amount, receive our URB size,
 * and proceed to issue several 1-character writes, assuming they will fit.
 * The very first write takes a complete URB. Fortunately, this only happens
 * when processing onlcr, so we only need 2 buffers. These values must be
 * powers of 2.
 */
#define XR_USB_SERIAL_NW  16
#define XR_USB_SERIAL_NR  16

struct xr_usb_serial_wb {
	unsigned char *buf;
	dma_addr_t dmah;
	int len;
	int use;
	struct urb		*urb;
	struct xr_usb_serial		*instance;
};

struct xr_usb_serial_rb {
	int			size;
	unsigned char		*base;
	dma_addr_t		dma;
	int			index;
	struct xr_usb_serial		*instance;
};

struct reg_addr_map {
	unsigned int    uart_enable_addr;
	unsigned int    uart_format_addr;
	unsigned int    uart_flow_addr;
	unsigned int    uart_loopback_addr;
    unsigned int    uart_xon_char_addr;
	unsigned int    uart_xoff_char_addr;
	unsigned int    uart_gpio_mode_addr;
	unsigned int    uart_gpio_dir_addr;
	unsigned int    uart_gpio_set_addr;
	unsigned int    uart_gpio_clr_addr;
	unsigned int    uart_gpio_status_addr;
	unsigned int    tx_break_addr;
	unsigned int    uart_custom_driver;
	unsigned int    uart_low_latency;
};

struct xr_usb_serial {
	struct usb_device *dev;				/* the corresponding usb device */
	struct usb_interface *control;			/* control interface */
	struct usb_interface *data;			/* data interface */
	struct tty_port port;			 	/* our tty port data */
	struct urb *ctrlurb;				/* urbs */
	u8 *ctrl_buffer;				/* buffers of urbs */
	dma_addr_t ctrl_dma;				/* dma handles of buffers */
	u8 *country_codes;				/* country codes from device */
	unsigned int country_code_size;			/* size of this buffer */
	unsigned int country_rel_date;			/* release date of version */
	struct xr_usb_serial_wb wb[XR_USB_SERIAL_NW];
	unsigned long read_urbs_free;
	struct urb *read_urbs[XR_USB_SERIAL_NR];
	struct xr_usb_serial_rb read_buffers[XR_USB_SERIAL_NR];
	int rx_buflimit;
	int rx_endpoint;
	spinlock_t read_lock;
	int write_used;					/* number of non-empty write buffers */
	int transmitting;
	spinlock_t write_lock;
	struct mutex mutex;
	bool disconnected;
	struct usb_cdc_line_coding line;		/* bits, stop, parity */
	struct work_struct work;			/* work queue entry for line discipline waking up */
	unsigned int ctrlin;				/* input control lines (DCD, DSR, RI, break, overruns) */
	unsigned int ctrlout;				/* output control lines (DTR, RTS) */
	unsigned int writesize;				/* max packet size for the output bulk endpoint */
	unsigned int readsize,ctrlsize;			/* buffer sizes for freeing */
	unsigned int minor;				/* xr_usb_serial minor number */
	unsigned char clocal;				/* termios CLOCAL */
	unsigned int ctrl_caps;				/* control capabilities from the class specific header */
	unsigned int susp_count;			/* number of suspended interfaces */
	unsigned int combined_interfaces:1;		/* control and data collapsed */
	unsigned int is_int_ep:1;			/* interrupt endpoints contrary to spec used */
	unsigned int throttled:1;			/* actually throttled */
	unsigned int throttle_req:1;			/* throttle requested */
	u8 bInterval;
	struct xr_usb_serial_wb *delayed_wb;			/* write queued for a device about to be woken */
	unsigned int channel;
	unsigned short DeviceVendor;
	unsigned short DeviceProduct;
	struct reg_addr_map reg_map;
};

#define CDC_DATA_INTERFACE_TYPE	0x0a

/* constants describing various quirks and errors */
#define NO_UNION_NORMAL			1
#define SINGLE_RX_URB			2
#define NO_CAP_LINE			4
#define NOT_A_MODEM			8
#define NO_DATA_INTERFACE		16
#define IGNORE_DEVICE			32


#define UART_ENABLE_TX                     1
#define UART_ENABLE_RX                     2

#define UART_GPIO_CLR_DTR                0x8
#define UART_GPIO_SET_DTR                0x8
#define UART_GPIO_CLR_RTS                0x20         
#define UART_GPIO_SET_RTS                0x20

#define LOOPBACK_ENABLE_TX_RX             1
#define LOOPBACK_ENABLE_RTS_CTS           2
#define LOOPBACK_ENABLE_DTR_DSR           4

#define UART_FLOW_MODE_NONE              0x0
#define UART_FLOW_MODE_HW                0x1
#define UART_FLOW_MODE_SW                0x2

#define UART_GPIO_MODE_SEL_GPIO          0x0
#define UART_GPIO_MODE_SEL_RTS_CTS       0x1

#define XR2280x_FUNC_MGR_OFFSET           0x40




