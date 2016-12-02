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
 * Copyright (c) 2015 Exar Corporation, Inc.
 *
 * This driver will work with any USB UART function in these Exar devices:
 *	XR21V1410/1412/1414
 *	XR21B1411
 *	XR21B1420/1422/1424
 *	XR22801/802/804
 *
 * The driver has been tested on various kernel versions from 3.6.x to 3.17.x.  
 * This driver may work on newer versions as well.  There is a different driver available 
 * from www.exar.com that will work with kernel versions 2.6.18 to 3.4.x.
 *
 * ChangeLog:
 *            Version 1A - Initial released version.
 */

//#undef DEBUG
#undef VERBOSE_DEBUG

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>
#include <linux/list.h>
#include "linux/version.h"

#include "xr_usb_serial_common.h"
#include "xr_usb_serial_ioctl.h"


#define DRIVER_AUTHOR "<uarttechsupport@exar.com>"
#define DRIVER_DESC "Exar USB UART (serial port) driver"

static struct usb_driver xr_usb_serial_driver;
static struct tty_driver *xr_usb_serial_tty_driver;
static struct xr_usb_serial *xr_usb_serial_table[XR_USB_SERIAL_TTY_MINORS];

static DEFINE_MUTEX(xr_usb_serial_table_lock);

/*
 * xr_usb_serial_table accessors
 */

/*
 * Look up an XR_USB_SERIAL structure by index. If found and not disconnected, increment
 * its refcount and return it with its mutex held.
 */
static struct xr_usb_serial *xr_usb_serial_get_by_index(unsigned index)
{
	struct xr_usb_serial *xr_usb_serial;

	mutex_lock(&xr_usb_serial_table_lock);
	xr_usb_serial = xr_usb_serial_table[index];
	if (xr_usb_serial) {
		mutex_lock(&xr_usb_serial->mutex);
		if (xr_usb_serial->disconnected) {
			mutex_unlock(&xr_usb_serial->mutex);
			xr_usb_serial = NULL;
		} else {
			tty_port_get(&xr_usb_serial->port);
			mutex_unlock(&xr_usb_serial->mutex);
		}
	}
	mutex_unlock(&xr_usb_serial_table_lock);
	return xr_usb_serial;
}

/*
 * Try to find an available minor number and if found, associate it with 'xr_usb_serial'.
 */
static int xr_usb_serial_alloc_minor(struct xr_usb_serial *xr_usb_serial)
{
	int minor;

	mutex_lock(&xr_usb_serial_table_lock);
	for (minor = 0; minor < XR_USB_SERIAL_TTY_MINORS; minor++) {
		if (!xr_usb_serial_table[minor]) {
			xr_usb_serial_table[minor] = xr_usb_serial;
			break;
		}
	}
	mutex_unlock(&xr_usb_serial_table_lock);

	return minor;
}

/* Release the minor number associated with 'xr_usb_serial'.  */
static void xr_usb_serial_release_minor(struct xr_usb_serial *xr_usb_serial)
{
	mutex_lock(&xr_usb_serial_table_lock);
	xr_usb_serial_table[xr_usb_serial->minor] = NULL;
	mutex_unlock(&xr_usb_serial_table_lock);
}

/*
 * Functions for XR_USB_SERIAL control messages.
 */

static int xr_usb_serial_ctrl_msg(struct xr_usb_serial *xr_usb_serial, int request, int value,
							void *buf, int len)
{
	int retval = usb_control_msg(xr_usb_serial->dev, usb_sndctrlpipe(xr_usb_serial->dev, 0),
		request, USB_RT_XR_USB_SERIAL, value,
		xr_usb_serial->control->altsetting[0].desc.bInterfaceNumber,
		buf, len, 5000);
	dev_dbg(&xr_usb_serial->control->dev,
			"%s - rq 0x%02x, val %#x, len %#x, result %d\n",
			__func__, request, value, len, retval);
	return retval < 0 ? retval : 0;
}

#include "xr_usb_serial_hal.c"


/*
 * Write buffer management.
 * All of these assume proper locks taken by the caller.
 */

static int xr_usb_serial_wb_alloc(struct xr_usb_serial *xr_usb_serial)
{
	int i, wbn;
	struct xr_usb_serial_wb *wb;

	wbn = 0;
	i = 0;
	for (;;) {
		wb = &xr_usb_serial->wb[wbn];
		if (!wb->use) {
			wb->use = 1;
			return wbn;
		}
		wbn = (wbn + 1) % XR_USB_SERIAL_NW;
		if (++i >= XR_USB_SERIAL_NW)
			return -1;
	}
}

static int xr_usb_serial_wb_is_avail(struct xr_usb_serial *xr_usb_serial)
{
	int i, n;
	unsigned long flags;

	n = XR_USB_SERIAL_NW;
	spin_lock_irqsave(&xr_usb_serial->write_lock, flags);
	for (i = 0; i < XR_USB_SERIAL_NW; i++)
		n -= xr_usb_serial->wb[i].use;
	spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);
	return n;
}

/*
 * Finish write. Caller must hold xr_usb_serial->write_lock
 */
static void xr_usb_serial_write_done(struct xr_usb_serial *xr_usb_serial, struct xr_usb_serial_wb *wb)
{
	wb->use = 0;
	xr_usb_serial->transmitting--;
	usb_autopm_put_interface_async(xr_usb_serial->control);
}

/*
 * Poke write.
 *
 * the caller is responsible for locking
 */

static int xr_usb_serial_start_wb(struct xr_usb_serial *xr_usb_serial, struct xr_usb_serial_wb *wb)
{
	int rc;

	xr_usb_serial->transmitting++;

	wb->urb->transfer_buffer = wb->buf;
	wb->urb->transfer_dma = wb->dmah;
	wb->urb->transfer_buffer_length = wb->len;
	wb->urb->dev = xr_usb_serial->dev;

	rc = usb_submit_urb(wb->urb, GFP_ATOMIC);
	if (rc < 0) {
		dev_err(&xr_usb_serial->data->dev,
			"%s - usb_submit_urb(write bulk) failed: %d\n",
			__func__, rc);
		xr_usb_serial_write_done(xr_usb_serial, wb);
	}
	return rc;
}

/*
 * attributes exported through sysfs
 */
static ssize_t show_caps
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);

	return sprintf(buf, "%d", xr_usb_serial->ctrl_caps);
}
static DEVICE_ATTR(bmCapabilities, S_IRUGO, show_caps, NULL);

static ssize_t show_country_codes
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);

	memcpy(buf, xr_usb_serial->country_codes, xr_usb_serial->country_code_size);
	return xr_usb_serial->country_code_size;
}

static DEVICE_ATTR(wCountryCodes, S_IRUGO, show_country_codes, NULL);

static ssize_t show_country_rel_date
(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct usb_interface *intf = to_usb_interface(dev);
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);

	return sprintf(buf, "%d", xr_usb_serial->country_rel_date);
}

static DEVICE_ATTR(iCountryCodeRelDate, S_IRUGO, show_country_rel_date, NULL);
/*
 * Interrupt handlers for various XR_USB_SERIAL device responses
 */

/* control interface reports status changes with "interrupt" transfers */
static void xr_usb_serial_ctrl_irq(struct urb *urb)
{
	struct xr_usb_serial *xr_usb_serial = urb->context;
	struct usb_cdc_notification *dr = urb->transfer_buffer;
	struct tty_struct *tty;
	unsigned char *data;
	int newctrl;
	int retval;
	int status = urb->status;
	int i;
	unsigned char *p;
		
	switch (status) {
	case 0:
		p = (unsigned char *)(urb->transfer_buffer);
		for(i=0;i<urb->actual_length;i++)
	    {
          dev_dbg(&xr_usb_serial->control->dev,"0x%02x\n",p[i]);
	    }
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_dbg(&xr_usb_serial->control->dev,
				"%s - urb shutting down with status: %d\n",
				__func__, status);
		return;
	default:
		dev_dbg(&xr_usb_serial->control->dev,
				"%s - nonzero urb status received: %d\n",
				__func__, status);
		goto exit;
	}

	usb_mark_last_busy(xr_usb_serial->dev);

	data = (unsigned char *)(dr + 1);
	switch (dr->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		dev_dbg(&xr_usb_serial->control->dev, "%s - network connection: %d\n",
							__func__, dr->wValue);
		break;

	case USB_CDC_NOTIFY_SERIAL_STATE:
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 9, 0)		
		newctrl = get_unaligned_le16(data);
    	if (!xr_usb_serial->clocal && (xr_usb_serial->ctrlin & ~newctrl & XR_USB_SERIAL_CTRL_DCD)) {
			dev_dbg(&xr_usb_serial->control->dev, "%s - calling hangup\n",
					__func__);
			tty_port_tty_hangup(&xr_usb_serial->port, false);
		}
#else		
		tty = tty_port_tty_get(&xr_usb_serial->port);
        newctrl = get_unaligned_le16(data);
		if (tty)
		{
			if (!xr_usb_serial->clocal &&
				(xr_usb_serial->ctrlin & ~newctrl & XR_USB_SERIAL_CTRL_DCD)) {
				dev_dbg(&xr_usb_serial->control->dev,
					"%s - calling hangup\n", __func__);
				tty_hangup(tty);
			}
			tty_kref_put(tty);
		}
#endif
		xr_usb_serial->ctrlin = newctrl;

		dev_dbg(&xr_usb_serial->control->dev,
			"%s - input control lines: dcd%c dsr%c break%c "
			"ring%c framing%c parity%c overrun%c\n",
			__func__,
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_DCD ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_DSR ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_BRK ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_RI  ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_FRAMING ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_PARITY ? '+' : '-',
			xr_usb_serial->ctrlin & XR_USB_SERIAL_CTRL_OVERRUN ? '+' : '-');
			break;

	default:
		dev_dbg(&xr_usb_serial->control->dev,
			"%s - unknown notification %d received: index %d "
			"len %d data0 %d data1 %d\n",
			__func__,
			dr->bNotificationType, dr->wIndex,
			dr->wLength, data[0], data[1]);
		break;
	}
exit:
	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&xr_usb_serial->control->dev, "%s - usb_submit_urb failed: %d\n",
							__func__, retval);
}

static int xr_usb_serial_submit_read_urb(struct xr_usb_serial *xr_usb_serial, int index, gfp_t mem_flags)
{
	int res;

	if (!test_and_clear_bit(index, &xr_usb_serial->read_urbs_free))
		return 0;

	dev_vdbg(&xr_usb_serial->data->dev, "%s - urb %d\n", __func__, index);

	res = usb_submit_urb(xr_usb_serial->read_urbs[index], mem_flags);
	if (res) {
		if (res != -EPERM) {
			dev_err(&xr_usb_serial->data->dev,
					"%s - usb_submit_urb failed: %d\n",
					__func__, res);
		}
		set_bit(index, &xr_usb_serial->read_urbs_free);
		return res;
	}

	return 0;
}

static int xr_usb_serial_submit_read_urbs(struct xr_usb_serial *xr_usb_serial, gfp_t mem_flags)
{
	int res;
	int i;

	for (i = 0; i < xr_usb_serial->rx_buflimit; ++i) {
		res = xr_usb_serial_submit_read_urb(xr_usb_serial, i, mem_flags);
		if (res)
			return res;
	}

	return 0;
}
static void xr_usb_serial_process_read_urb(struct xr_usb_serial *xr_usb_serial, struct urb *urb)
{
    struct tty_struct *tty;
	if (!urb->actual_length)
		return;
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 9, 0)    
	tty_insert_flip_string(&xr_usb_serial->port, urb->transfer_buffer,
			urb->actual_length);
	tty_flip_buffer_push(&xr_usb_serial->port);
#else
    tty = tty_port_tty_get(&xr_usb_serial->port);
	if (!tty)
		return;
	tty_insert_flip_string(tty, urb->transfer_buffer, urb->actual_length);
	tty_flip_buffer_push(tty);

	tty_kref_put(tty);
#endif
}

static void xr_usb_serial_read_bulk_callback(struct urb *urb)
{
	struct xr_usb_serial_rb *rb = urb->context;
	struct xr_usb_serial *xr_usb_serial = rb->instance;
	unsigned long flags;

	dev_vdbg(&xr_usb_serial->data->dev, "%s - urb %d, len %d\n", __func__,
					rb->index, urb->actual_length);
	set_bit(rb->index, &xr_usb_serial->read_urbs_free);

	if (!xr_usb_serial->dev) {
		dev_dbg(&xr_usb_serial->data->dev, "%s - disconnected\n", __func__);
		return;
	}
	usb_mark_last_busy(xr_usb_serial->dev);

	if (urb->status) {
		dev_dbg(&xr_usb_serial->data->dev, "%s - non-zero urb status: %d\n",
							__func__, urb->status);
		return;
	}
	xr_usb_serial_process_read_urb(xr_usb_serial, urb);

	/* throttle device if requested by tty */
	spin_lock_irqsave(&xr_usb_serial->read_lock, flags);
	xr_usb_serial->throttled = xr_usb_serial->throttle_req;
	if (!xr_usb_serial->throttled && !xr_usb_serial->susp_count) {
		spin_unlock_irqrestore(&xr_usb_serial->read_lock, flags);
		xr_usb_serial_submit_read_urb(xr_usb_serial, rb->index, GFP_ATOMIC);
	} else {
		spin_unlock_irqrestore(&xr_usb_serial->read_lock, flags);
	}
}

/* data interface wrote those outgoing bytes */
static void xr_usb_serial_write_bulk(struct urb *urb)
{
	struct xr_usb_serial_wb *wb = urb->context;
	struct xr_usb_serial *xr_usb_serial = wb->instance;
	unsigned long flags;

	if (urb->status	|| (urb->actual_length != urb->transfer_buffer_length))
		dev_vdbg(&xr_usb_serial->data->dev, "%s - len %d/%d, status %d\n",
			__func__,
			urb->actual_length,
			urb->transfer_buffer_length,
			urb->status);

	spin_lock_irqsave(&xr_usb_serial->write_lock, flags);
	xr_usb_serial_write_done(xr_usb_serial, wb);
	spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);
	schedule_work(&xr_usb_serial->work);
}

static void xr_usb_serial_softint(struct work_struct *work)
{
	struct xr_usb_serial *xr_usb_serial = container_of(work, struct xr_usb_serial, work);
    struct tty_struct *tty;
	
	dev_vdbg(&xr_usb_serial->data->dev, "%s\n", __func__);
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 9, 0)
	tty_port_tty_wakeup(&xr_usb_serial->port);
#else	
	tty = tty_port_tty_get(&xr_usb_serial->port);
	if (!tty)
		return;
	tty_wakeup(tty);
	tty_kref_put(tty);
#endif	
}

/*
 * TTY handlers
 */

static int xr_usb_serial_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial;
	int retval;

	dev_dbg(tty->dev, "%s\n", __func__);

	xr_usb_serial = xr_usb_serial_get_by_index(tty->index);
	if (!xr_usb_serial)
		return -ENODEV;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error_init_termios;

	tty->driver_data = xr_usb_serial;

	return 0;

error_init_termios:
	tty_port_put(&xr_usb_serial->port);
	return retval;
}

static int xr_usb_serial_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;

	dev_dbg(tty->dev, "%s\n", __func__);

	return tty_port_open(&xr_usb_serial->port, tty, filp);
}

static int xr_usb_serial_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = container_of(port, struct xr_usb_serial, port);
	int retval = -ENODEV;

	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);

	mutex_lock(&xr_usb_serial->mutex);
	if (xr_usb_serial->disconnected)
		goto disconnected;

	retval = usb_autopm_get_interface(xr_usb_serial->control);
	if (retval)
		goto error_get_interface;

	/*
	 * FIXME: Why do we need this? Allocating 64K of physically contiguous
	 * memory is really nasty...
	 */
	set_bit(TTY_NO_WRITE_SPLIT, &tty->flags);
	xr_usb_serial->control->needs_remote_wakeup = 1;

	xr_usb_serial->ctrlurb->dev = xr_usb_serial->dev;
	if (usb_submit_urb(xr_usb_serial->ctrlurb, GFP_KERNEL)) {
		dev_err(&xr_usb_serial->control->dev,
			"%s - usb_submit_urb(ctrl irq) failed\n", __func__);
		goto error_submit_urb;
	}

	xr_usb_serial->ctrlout = XR_USB_SERIAL_CTRL_DTR | XR_USB_SERIAL_CTRL_RTS;
	if (xr_usb_serial_set_control(xr_usb_serial, xr_usb_serial->ctrlout) < 0 &&
	    (xr_usb_serial->ctrl_caps & USB_CDC_CAP_LINE))
		goto error_set_control;

	usb_autopm_put_interface(xr_usb_serial->control);

	/*
	 * Unthrottle device in case the TTY was closed while throttled.
	 */
	spin_lock_irq(&xr_usb_serial->read_lock);
	xr_usb_serial->throttled = 0;
	xr_usb_serial->throttle_req = 0;
	spin_unlock_irq(&xr_usb_serial->read_lock);

	if (xr_usb_serial_submit_read_urbs(xr_usb_serial, GFP_KERNEL))
		goto error_submit_read_urbs;

	mutex_unlock(&xr_usb_serial->mutex);

	return 0;

error_submit_read_urbs:
	xr_usb_serial->ctrlout = 0;
	xr_usb_serial_set_control(xr_usb_serial, xr_usb_serial->ctrlout);
error_set_control:
	usb_kill_urb(xr_usb_serial->ctrlurb);
error_submit_urb:
	usb_autopm_put_interface(xr_usb_serial->control);
error_get_interface:
disconnected:
	mutex_unlock(&xr_usb_serial->mutex);
	return retval;
}

static void xr_usb_serial_port_destruct(struct tty_port *port)
{
	struct xr_usb_serial *xr_usb_serial = container_of(port, struct xr_usb_serial, port);

	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);
    #if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
	tty_unregister_device(xr_usb_serial_tty_driver, xr_usb_serial->minor);
	#endif
	xr_usb_serial_release_minor(xr_usb_serial);
	usb_put_intf(xr_usb_serial->control);
	kfree(xr_usb_serial->country_codes);
	kfree(xr_usb_serial);
}

static void xr_usb_serial_port_shutdown(struct tty_port *port)
{
	struct xr_usb_serial *xr_usb_serial = container_of(port, struct xr_usb_serial, port);
	int i;

	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);

	mutex_lock(&xr_usb_serial->mutex);
	if (!xr_usb_serial->disconnected) {
		usb_autopm_get_interface(xr_usb_serial->control);
		xr_usb_serial_set_control(xr_usb_serial, xr_usb_serial->ctrlout = 0);
		usb_kill_urb(xr_usb_serial->ctrlurb);
		for (i = 0; i < XR_USB_SERIAL_NW; i++)
			usb_kill_urb(xr_usb_serial->wb[i].urb);
		for (i = 0; i < xr_usb_serial->rx_buflimit; i++)
			usb_kill_urb(xr_usb_serial->read_urbs[i]);
		xr_usb_serial->control->needs_remote_wakeup = 0;
		usb_autopm_put_interface(xr_usb_serial->control);
	}
	mutex_unlock(&xr_usb_serial->mutex);
}

static void xr_usb_serial_tty_cleanup(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);
	tty_port_put(&xr_usb_serial->port);
}

static void xr_usb_serial_tty_hangup(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);
	tty_port_hangup(&xr_usb_serial->port);
}

static void xr_usb_serial_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);
	tty_port_close(&xr_usb_serial->port, tty, filp);
}

static int xr_usb_serial_tty_write(struct tty_struct *tty,
					const unsigned char *buf, int count)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	int stat;
	unsigned long flags;
	int wbn;
	struct xr_usb_serial_wb *wb;

	if (!count)
		return 0;

	dev_vdbg(&xr_usb_serial->data->dev, "%s - count %d\n", __func__, count);

	spin_lock_irqsave(&xr_usb_serial->write_lock, flags);
	wbn = xr_usb_serial_wb_alloc(xr_usb_serial);
	if (wbn < 0) {
		spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);
		return 0;
	}
	wb = &xr_usb_serial->wb[wbn];

	if (!xr_usb_serial->dev) {
		wb->use = 0;
		spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);
		return -ENODEV;
	}

	count = (count > xr_usb_serial->writesize) ? xr_usb_serial->writesize : count;
	dev_vdbg(&xr_usb_serial->data->dev, "%s - write %d\n", __func__, count);
	memcpy(wb->buf, buf, count);
	wb->len = count;

	usb_autopm_get_interface_async(xr_usb_serial->control);
	if (xr_usb_serial->susp_count) {
		if (!xr_usb_serial->delayed_wb)
			xr_usb_serial->delayed_wb = wb;
		else
			usb_autopm_put_interface_async(xr_usb_serial->control);
		spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);
		return count;	/* A white lie */
	}
	usb_mark_last_busy(xr_usb_serial->dev);

	stat = xr_usb_serial_start_wb(xr_usb_serial, wb);
	spin_unlock_irqrestore(&xr_usb_serial->write_lock, flags);

	if (stat < 0)
		return stat;
	return count;
}

static int xr_usb_serial_tty_write_room(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	/*
	 * Do not let the line discipline to know that we have a reserve,
	 * or it might get too enthusiastic.
	 */
	return xr_usb_serial_wb_is_avail(xr_usb_serial) ? xr_usb_serial->writesize : 0;
}

static int xr_usb_serial_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	/*
	 * if the device was unplugged then any remaining characters fell out
	 * of the connector ;)
	 */
	if (xr_usb_serial->disconnected)
		return 0;
	/*
	 * This is inaccurate (overcounts), but it works.
	 */
	return (XR_USB_SERIAL_NW - xr_usb_serial_wb_is_avail(xr_usb_serial)) * xr_usb_serial->writesize;
}

static void xr_usb_serial_tty_throttle(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;

	spin_lock_irq(&xr_usb_serial->read_lock);
	xr_usb_serial->throttle_req = 1;
	spin_unlock_irq(&xr_usb_serial->read_lock);
}

static void xr_usb_serial_tty_unthrottle(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	unsigned int was_throttled;

	spin_lock_irq(&xr_usb_serial->read_lock);
	was_throttled = xr_usb_serial->throttled;
	xr_usb_serial->throttled = 0;
	xr_usb_serial->throttle_req = 0;
	spin_unlock_irq(&xr_usb_serial->read_lock);

	if (was_throttled)
		xr_usb_serial_submit_read_urbs(xr_usb_serial, GFP_KERNEL);
}

static int xr_usb_serial_tty_break_ctl(struct tty_struct *tty, int state)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	int retval;

	retval = xr_usb_serial_send_break(xr_usb_serial, state ? 0xffff : 0);
	if (retval < 0)
		dev_dbg(&xr_usb_serial->control->dev, "%s - send break failed\n",
								__func__);
	return retval;
}

static int xr_usb_serial_tty_tiocmget(struct tty_struct *tty)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	dev_dbg(&xr_usb_serial->control->dev, "xr_usb_serial_tty_tiocmget\n");
    return xr_usb_serial_tiocmget(xr_usb_serial);

}

static int xr_usb_serial_tty_tiocmset(struct tty_struct *tty,
			    unsigned int set, unsigned int clear)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	dev_dbg(&xr_usb_serial->control->dev, "xr_usb_serial_tty_tiocmset set=0x%x clear=0x%x\n",set,clear);
    return xr_usb_serial_tiocmset(xr_usb_serial,set,clear);

}

static int get_serial_info(struct xr_usb_serial *xr_usb_serial, struct serial_struct __user *info)
{
	struct serial_struct tmp;

	if (!info)
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	tmp.flags = ASYNC_LOW_LATENCY;
	tmp.xmit_fifo_size = xr_usb_serial->writesize;
	tmp.baud_base = le32_to_cpu(xr_usb_serial->line.dwDTERate);
	tmp.close_delay	= xr_usb_serial->port.close_delay / 10;
	tmp.closing_wait = xr_usb_serial->port.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				ASYNC_CLOSING_WAIT_NONE :
				xr_usb_serial->port.closing_wait / 10;

	if (copy_to_user(info, &tmp, sizeof(tmp)))
		return -EFAULT;
	else
		return 0;
}

static int set_serial_info(struct xr_usb_serial *xr_usb_serial,
				struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	unsigned int closing_wait, close_delay;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	close_delay = new_serial.close_delay * 10;
	closing_wait = new_serial.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE : new_serial.closing_wait * 10;

	mutex_lock(&xr_usb_serial->port.mutex);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((close_delay != xr_usb_serial->port.close_delay) ||
		    (closing_wait != xr_usb_serial->port.closing_wait))
			retval = -EPERM;
		else
			retval = -EOPNOTSUPP;
	} else {
		xr_usb_serial->port.close_delay  = close_delay;
		xr_usb_serial->port.closing_wait = closing_wait;
	}

	mutex_unlock(&xr_usb_serial->port.mutex);
	return retval;
}

static int xr_usb_serial_tty_ioctl(struct tty_struct *tty,
					unsigned int cmd, unsigned long arg)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
	int rv = -ENOIOCTLCMD;
    unsigned int  channel, reg, val;

    short	*data;
	switch (cmd) {
	case TIOCGSERIAL: /* gets serial port data */
		rv = get_serial_info(xr_usb_serial, (struct serial_struct __user *) arg);
		break;
	case TIOCSSERIAL:
		rv = set_serial_info(xr_usb_serial, (struct serial_struct __user *) arg);
		break;
    case XR_USB_SERIAL_GET_REG:
                if (get_user(channel, (int __user *)arg))
                        return -EFAULT;
                if (get_user(reg, (int __user *)(arg + sizeof(int))))
                        return -EFAULT;

                data = kmalloc(2, GFP_KERNEL);
                if (data == NULL) {
                        dev_err(&xr_usb_serial->control->dev, "%s - Cannot allocate USB buffer.\n", __func__);
                        return -ENOMEM;
		}
        			
		        if (channel == -1)
		        {
		          rv = xr_usb_serial_get_reg(xr_usb_serial,reg, data);
		        }
				else
				{
			  	  rv = xr_usb_serial_get_reg_ext(xr_usb_serial,channel,reg, data);
				}
                if (rv != 1) {
                        dev_err(&xr_usb_serial->control->dev, "Cannot get register (%d)\n", rv);
                        kfree(data);
                        return -EFAULT;
                }
				if (put_user(le16_to_cpu(*data), (int __user *)(arg + 2 * sizeof(int))))
              	{
                   dev_err(&xr_usb_serial->control->dev, "Cannot put user result\n");
                   kfree(data);
                   return -EFAULT;
                }
                rv = 0;
                kfree(data);
                break;

      case XR_USB_SERIAL_SET_REG:
                if (get_user(channel, (int __user *)arg))
                        return -EFAULT;
                if (get_user(reg, (int __user *)(arg + sizeof(int))))
                        return -EFAULT;
                if (get_user(val, (int __user *)(arg + 2 * sizeof(int))))
                        return -EFAULT;

			if (channel == -1)
			{
				rv = xr_usb_serial_set_reg(xr_usb_serial,reg, val);
			}
			else
			{
			 	rv = xr_usb_serial_set_reg_ext(xr_usb_serial,channel,reg, val);
				
			}
		    if (rv < 0)
               return -EFAULT;  
			rv = 0;
            break;
	case XR_USB_SERIAL_LOOPBACK:
		     if (get_user(channel, (int __user *)arg))
                        return -EFAULT;
		     if (channel == -1)
			   channel = xr_usb_serial->channel;
			 rv = xr_usb_serial_set_loopback(xr_usb_serial,channel);
			 if (rv < 0)
               return -EFAULT;
			 rv = 0;
		     break;
		
	}

	return rv;
}

static void xr_usb_serial_tty_set_termios(struct tty_struct *tty,
						struct ktermios *termios_old)
{
	struct xr_usb_serial *xr_usb_serial = tty->driver_data;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)	
	struct ktermios *termios = tty->termios;
#else
    struct ktermios *termios = &tty->termios;
#endif
	unsigned int   cflag = termios->c_cflag;
	struct usb_cdc_line_coding newline;
	int newctrl = xr_usb_serial->ctrlout;
    xr_usb_serial_disable(xr_usb_serial);
	newline.dwDTERate = cpu_to_le32(tty_get_baud_rate(tty));
	newline.bCharFormat = termios->c_cflag & CSTOPB ? 1 : 0;
	newline.bParityType = termios->c_cflag & PARENB ?
				(termios->c_cflag & PARODD ? 1 : 2) +
				(termios->c_cflag & CMSPAR ? 2 : 0) : 0;
	switch (termios->c_cflag & CSIZE) {
	case CS5:/*using CS5 replace of the 9 bit data mode*/
		newline.bDataBits = 9;
		break;
	case CS6:
		newline.bDataBits = 6;
		break;
	case CS7:
		newline.bDataBits = 7;
		break;
	case CS8:
	default:
		newline.bDataBits = 8;
		break;
	}
	/* FIXME: Needs to clear unsupported bits in the termios */
	xr_usb_serial->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (!newline.dwDTERate) {
		newline.dwDTERate = xr_usb_serial->line.dwDTERate;
		newctrl &= ~XR_USB_SERIAL_CTRL_DTR;
	} else
		newctrl |=  XR_USB_SERIAL_CTRL_DTR;

	if (newctrl != xr_usb_serial->ctrlout)
		xr_usb_serial_set_control(xr_usb_serial, xr_usb_serial->ctrlout = newctrl);
	
    xr_usb_serial_set_flow_mode(xr_usb_serial,tty,cflag);/*set the serial flow mode*/
	 	
	if (memcmp(&xr_usb_serial->line, &newline, sizeof newline))
	{
		memcpy(&xr_usb_serial->line, &newline, sizeof newline);
		dev_dbg(&xr_usb_serial->control->dev, "%s - set line: %d %d %d %d\n",
			__func__,
			le32_to_cpu(newline.dwDTERate),
			newline.bCharFormat, newline.bParityType,
			newline.bDataBits);
		xr_usb_serial_set_line(xr_usb_serial, &xr_usb_serial->line);
	}
	xr_usb_serial_enable(xr_usb_serial);
}

static const struct tty_port_operations xr_usb_serial_port_ops = {
	.shutdown = xr_usb_serial_port_shutdown,
	.activate = xr_usb_serial_port_activate,
	.destruct = xr_usb_serial_port_destruct,
};

/*
 * USB probe and disconnect routines.
 */

/* Little helpers: write/read buffers free */
static void xr_usb_serial_write_buffers_free(struct xr_usb_serial *xr_usb_serial)
{
	int i;
	struct xr_usb_serial_wb *wb;
	struct usb_device *usb_dev = interface_to_usbdev(xr_usb_serial->control);

	for (wb = &xr_usb_serial->wb[0], i = 0; i < XR_USB_SERIAL_NW; i++, wb++)
		usb_free_coherent(usb_dev, xr_usb_serial->writesize, wb->buf, wb->dmah);
}

static void xr_usb_serial_read_buffers_free(struct xr_usb_serial *xr_usb_serial)
{
	struct usb_device *usb_dev = interface_to_usbdev(xr_usb_serial->control);
	int i;

	for (i = 0; i < xr_usb_serial->rx_buflimit; i++)
		usb_free_coherent(usb_dev, xr_usb_serial->readsize,
			  xr_usb_serial->read_buffers[i].base, xr_usb_serial->read_buffers[i].dma);
}

/* Little helper: write buffers allocate */
static int xr_usb_serial_write_buffers_alloc(struct xr_usb_serial *xr_usb_serial)
{
	int i;
	struct xr_usb_serial_wb *wb;

	for (wb = &xr_usb_serial->wb[0], i = 0; i < XR_USB_SERIAL_NW; i++, wb++) {
		wb->buf = usb_alloc_coherent(xr_usb_serial->dev, xr_usb_serial->writesize, GFP_KERNEL,
		    &wb->dmah);
		if (!wb->buf) {
			while (i != 0) {
				--i;
				--wb;
				usb_free_coherent(xr_usb_serial->dev, xr_usb_serial->writesize,
				    wb->buf, wb->dmah);
			}
			return -ENOMEM;
		}
	}
	return 0;
}

static int xr_usb_serial_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_cdc_union_desc *union_header = NULL;
	struct usb_cdc_country_functional_desc *cfd = NULL;
	unsigned char *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	struct usb_interface *control_interface;
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct xr_usb_serial *xr_usb_serial;
	int minor;
	int ctrlsize, readsize;
	u8 *buf;
	u8 ac_management_function = 0;
	u8 call_management_function = 0;
	int call_interface_num = -1;
	int data_interface_num = -1;
	unsigned long quirks;
	int num_rx_buf;
	int i;
	int combined_interfaces = 0;
	struct device *tty_dev;
	int rv = -ENOMEM;

	/* normal quirks */
	quirks = (unsigned long)id->driver_info;

	if (quirks == IGNORE_DEVICE)
		return -ENODEV;

	num_rx_buf = (quirks == SINGLE_RX_URB) ? 1 : XR_USB_SERIAL_NR;
	
    dev_dbg(&intf->dev, "USB_device_id idVendor:%04x, idProduct %04x\n",id->idVendor,id->idProduct);
	
	/* handle quirks deadly to normal probing*/
	if (quirks == NO_UNION_NORMAL) {
		data_interface = usb_ifnum_to_if(usb_dev, 1);
		control_interface = usb_ifnum_to_if(usb_dev, 0);
		goto skip_normal_probe;
	}

	/* normal probing*/
	if (!buffer) {
		dev_err(&intf->dev, "Weird descriptor references\n");
		return -EINVAL;
	}

	if (!buflen) {
		if (intf->cur_altsetting->endpoint &&
				intf->cur_altsetting->endpoint->extralen &&
				intf->cur_altsetting->endpoint->extra) {
			dev_dbg(&intf->dev,
				"Seeking extra descriptors on endpoint\n");
			buflen = intf->cur_altsetting->endpoint->extralen;
			buffer = intf->cur_altsetting->endpoint->extra;
		} else {
			dev_err(&intf->dev,
				"Zero length descriptor references\n");
			return -EINVAL;
		}
	}

	while (buflen > 0) {
		if (buffer[1] != USB_DT_CS_INTERFACE) {
			dev_err(&intf->dev, "skipping garbage\n");
			goto next_desc;
		}

		switch (buffer[2]) {
		case USB_CDC_UNION_TYPE: /* we've found it */
			if (union_header) {
				dev_err(&intf->dev, "More than one "
					"union descriptor, skipping ...\n");
				goto next_desc;
			}
			union_header = (struct usb_cdc_union_desc *)buffer;
			break;
		case USB_CDC_COUNTRY_TYPE: /* export through sysfs*/
			cfd = (struct usb_cdc_country_functional_desc *)buffer;
			break;
		case USB_CDC_HEADER_TYPE: /* maybe check version */
			break; /* for now we ignore it */
		case USB_CDC_ACM_TYPE:
			ac_management_function = buffer[3];
			break;
		case USB_CDC_CALL_MANAGEMENT_TYPE:
			call_management_function = buffer[3];
			call_interface_num = buffer[4];
			if ((quirks & NOT_A_MODEM) == 0 && (call_management_function & 3) != 3)
				dev_err(&intf->dev, "This device cannot do calls on its own. It is not a modem.\n");
			break;
		default:
			/* there are LOTS more CDC descriptors that
			 * could legitimately be found here.
			 */
			dev_dbg(&intf->dev, "Ignoring descriptor: "
					"type %02x, length %d\n",
					buffer[2], buffer[0]);
			break;
		}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (!union_header) {
		if (call_interface_num > 0) {
			dev_dbg(&intf->dev, "No union descriptor, using call management descriptor\n");
			/* quirks for Droids MuIn LCD */
			if (quirks & NO_DATA_INTERFACE)
				data_interface = usb_ifnum_to_if(usb_dev, 0);
			else
				data_interface = usb_ifnum_to_if(usb_dev, (data_interface_num = call_interface_num));
			control_interface = intf;
		} else {
			if (intf->cur_altsetting->desc.bNumEndpoints != 3) {
				dev_dbg(&intf->dev,"No union descriptor, giving up\n");
				return -ENODEV;
			} else {
				dev_warn(&intf->dev,"No union descriptor, testing for castrated device\n");
				combined_interfaces = 1;
				control_interface = data_interface = intf;
				goto look_for_collapsed_interface;
			}
		}
	} else {
		control_interface = usb_ifnum_to_if(usb_dev, union_header->bMasterInterface0);
		data_interface = usb_ifnum_to_if(usb_dev, (data_interface_num = union_header->bSlaveInterface0));
		if (!control_interface || !data_interface) {
			dev_dbg(&intf->dev, "no interfaces\n");
			return -ENODEV;
		}
	}

	if (data_interface_num != call_interface_num)
		dev_dbg(&intf->dev, "Separate call control interface. That is not fully supported.\n");

	if (control_interface == data_interface) {
		/* some broken devices designed for windows work this way */
		dev_warn(&intf->dev,"Control and data interfaces are not separated!\n");
		combined_interfaces = 1;
		/* a popular other OS doesn't use it */
		quirks |= NO_CAP_LINE;
		if (data_interface->cur_altsetting->desc.bNumEndpoints != 3) {
			dev_err(&intf->dev, "This needs exactly 3 endpoints\n");
			return -EINVAL;
		}
look_for_collapsed_interface:
		for (i = 0; i < 3; i++) {
			struct usb_endpoint_descriptor *ep;
			ep = &data_interface->cur_altsetting->endpoint[i].desc;

			if (usb_endpoint_is_int_in(ep))
				epctrl = ep;
			else if (usb_endpoint_is_bulk_out(ep))
				epwrite = ep;
			else if (usb_endpoint_is_bulk_in(ep))
				epread = ep;
			else
				return -EINVAL;
		}
		if (!epctrl || !epread || !epwrite)
			return -ENODEV;
		else
			goto made_compressed_probe;
	}

skip_normal_probe:

	/*workaround for switched interfaces */
	if (data_interface->cur_altsetting->desc.bInterfaceClass
						!= CDC_DATA_INTERFACE_TYPE) {
		if (control_interface->cur_altsetting->desc.bInterfaceClass
						== CDC_DATA_INTERFACE_TYPE) {
			struct usb_interface *t;
			dev_dbg(&intf->dev,
				"Your device has switched interfaces.\n");
			t = control_interface;
			control_interface = data_interface;
			data_interface = t;
		} else {
			return -EINVAL;
		}
	}

	/* Accept probe requests only for the control interface */
	if (!combined_interfaces && intf != control_interface)
		return -ENODEV;

	if (!combined_interfaces && usb_interface_claimed(data_interface)) {
		/* valid in this context */
		dev_dbg(&intf->dev, "The data interface isn't available\n");
		return -EBUSY;
	}


	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2 ||
	    control_interface->cur_altsetting->desc.bNumEndpoints == 0)
		return -EINVAL;

	epctrl = &control_interface->cur_altsetting->endpoint[0].desc;
	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;


	/* workaround for switched endpoints */
	if (!usb_endpoint_dir_in(epread)) {
		/* descriptors are swapped */
		struct usb_endpoint_descriptor *t;
		dev_dbg(&intf->dev,
			"The data interface has switched endpoints\n");
		t = epread;
		epread = epwrite;
		epwrite = t;
	}
made_compressed_probe:
	dev_dbg(&intf->dev, "interfaces are valid\n");

	xr_usb_serial = kzalloc(sizeof(struct xr_usb_serial), GFP_KERNEL);
	if (xr_usb_serial == NULL) {
		dev_err(&intf->dev, "out of memory (xr_usb_serial kzalloc)\n");
		goto alloc_fail;
	}

	minor = xr_usb_serial_alloc_minor(xr_usb_serial);
	if (minor == XR_USB_SERIAL_TTY_MINORS) {
		dev_err(&intf->dev, "no more free xr_usb_serial devices\n");
		kfree(xr_usb_serial);
		return -ENODEV;
	}

	ctrlsize = usb_endpoint_maxp(epctrl);
	readsize = usb_endpoint_maxp(epread) *
				(quirks == SINGLE_RX_URB ? 1 : 2);
	xr_usb_serial->combined_interfaces = combined_interfaces;
	xr_usb_serial->writesize = usb_endpoint_maxp(epwrite) * 20;
	xr_usb_serial->control = control_interface;
	xr_usb_serial->data = data_interface;
	xr_usb_serial->minor = minor;
	xr_usb_serial->dev = usb_dev;
	xr_usb_serial->ctrl_caps = ac_management_function;
	if (quirks & NO_CAP_LINE)
		xr_usb_serial->ctrl_caps &= ~USB_CDC_CAP_LINE;
	xr_usb_serial->ctrlsize = ctrlsize;
	xr_usb_serial->readsize = readsize;
	xr_usb_serial->rx_buflimit = num_rx_buf;
	INIT_WORK(&xr_usb_serial->work, xr_usb_serial_softint);
	spin_lock_init(&xr_usb_serial->write_lock);
	spin_lock_init(&xr_usb_serial->read_lock);
	mutex_init(&xr_usb_serial->mutex);
	xr_usb_serial->rx_endpoint = usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);
	xr_usb_serial->is_int_ep = usb_endpoint_xfer_int(epread);
	if (xr_usb_serial->is_int_ep)
		xr_usb_serial->bInterval = epread->bInterval;
	tty_port_init(&xr_usb_serial->port);
	xr_usb_serial->port.ops = &xr_usb_serial_port_ops;
	xr_usb_serial->DeviceVendor = id->idVendor;
	xr_usb_serial->DeviceProduct = id->idProduct;
	#if 0
	if((xr_usb_serial->DeviceProduct&0xfff0) == 0x1410)
	{//map the serial port A B C D to blocknum 0 1 2 3 for the xr21v141x device
	    xr_usb_serial->channel = epwrite->bEndpointAddress - 1;
	}
	else if((xr_usb_serial->DeviceProduct&0xfff0) == 0x1420)
	{//map the serial port A B C D to blocknum 0 2 4 6 for the xr21B142x device
	    xr_usb_serial->channel = (epwrite->bEndpointAddress - 4)*2;
	}
	else
	{
	   xr_usb_serial->channel = epwrite->bEndpointAddress;
	}
	#else
	xr_usb_serial->channel = epwrite->bEndpointAddress;
	dev_dbg(&intf->dev, "epwrite->bEndpointAddress =%d\n",epwrite->bEndpointAddress);
	#endif
	buf = usb_alloc_coherent(usb_dev, ctrlsize, GFP_KERNEL, &xr_usb_serial->ctrl_dma);
	if (!buf) {
		dev_err(&intf->dev, "out of memory (ctrl buffer alloc)\n");
		goto alloc_fail2;
	}
	xr_usb_serial->ctrl_buffer = buf;

	if (xr_usb_serial_write_buffers_alloc(xr_usb_serial) < 0) {
		dev_err(&intf->dev, "out of memory (write buffer alloc)\n");
		goto alloc_fail4;
	}

	xr_usb_serial->ctrlurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!xr_usb_serial->ctrlurb) {
		dev_err(&intf->dev, "out of memory (ctrlurb kmalloc)\n");
		goto alloc_fail5;
	}
	for (i = 0; i < num_rx_buf; i++) {
		struct xr_usb_serial_rb *rb = &(xr_usb_serial->read_buffers[i]);
		struct urb *urb;

		rb->base = usb_alloc_coherent(xr_usb_serial->dev, readsize, GFP_KERNEL,
								&rb->dma);
		if (!rb->base) {
			dev_err(&intf->dev, "out of memory "
					"(read bufs usb_alloc_coherent)\n");
			goto alloc_fail6;
		}
		rb->index = i;
		rb->instance = xr_usb_serial;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			dev_err(&intf->dev,
				"out of memory (read urbs usb_alloc_urb)\n");
			goto alloc_fail6;
		}
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		urb->transfer_dma = rb->dma;
		if (xr_usb_serial->is_int_ep) {
			usb_fill_int_urb(urb, xr_usb_serial->dev,
					 xr_usb_serial->rx_endpoint,
					 rb->base,
					 xr_usb_serial->readsize,
					 xr_usb_serial_read_bulk_callback, rb,
					 xr_usb_serial->bInterval);
		} else {
			usb_fill_bulk_urb(urb, xr_usb_serial->dev,
					  xr_usb_serial->rx_endpoint,
					  rb->base,
					  xr_usb_serial->readsize,
					  xr_usb_serial_read_bulk_callback, rb);
		}

		xr_usb_serial->read_urbs[i] = urb;
		__set_bit(i, &xr_usb_serial->read_urbs_free);
	}
	for (i = 0; i < XR_USB_SERIAL_NW; i++) {
		struct xr_usb_serial_wb *snd = &(xr_usb_serial->wb[i]);

		snd->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (snd->urb == NULL) {
			dev_err(&intf->dev,
				"out of memory (write urbs usb_alloc_urb)\n");
			goto alloc_fail7;
		}

		if (usb_endpoint_xfer_int(epwrite))
			usb_fill_int_urb(snd->urb, usb_dev,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)			
				usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress),
#else
                usb_sndintpipe(usb_dev, epwrite->bEndpointAddress),
#endif
				NULL, xr_usb_serial->writesize, xr_usb_serial_write_bulk, snd, epwrite->bInterval);
		else
			usb_fill_bulk_urb(snd->urb, usb_dev,
				usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress),
				NULL, xr_usb_serial->writesize, xr_usb_serial_write_bulk, snd);
		snd->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		snd->instance = xr_usb_serial;
	}

	usb_set_intfdata(intf, xr_usb_serial);

	i = device_create_file(&intf->dev, &dev_attr_bmCapabilities);
	if (i < 0)
		goto alloc_fail7;

	if (cfd) { /* export the country data */
		xr_usb_serial->country_codes = kmalloc(cfd->bLength - 4, GFP_KERNEL);
		if (!xr_usb_serial->country_codes)
			goto skip_countries;
		xr_usb_serial->country_code_size = cfd->bLength - 4;
		memcpy(xr_usb_serial->country_codes, (u8 *)&cfd->wCountyCode0,
							cfd->bLength - 4);
		xr_usb_serial->country_rel_date = cfd->iCountryCodeRelDate;

		i = device_create_file(&intf->dev, &dev_attr_wCountryCodes);
		if (i < 0) {
			kfree(xr_usb_serial->country_codes);
			xr_usb_serial->country_codes = NULL;
			xr_usb_serial->country_code_size = 0;
			goto skip_countries;
		}

		i = device_create_file(&intf->dev,
						&dev_attr_iCountryCodeRelDate);
		if (i < 0) {
			device_remove_file(&intf->dev, &dev_attr_wCountryCodes);
			kfree(xr_usb_serial->country_codes);
			xr_usb_serial->country_codes = NULL;
			xr_usb_serial->country_code_size = 0;
			goto skip_countries;
		}
	}

skip_countries:
	usb_fill_int_urb(xr_usb_serial->ctrlurb, usb_dev,
			 usb_rcvintpipe(usb_dev, epctrl->bEndpointAddress),
			 xr_usb_serial->ctrl_buffer, ctrlsize, xr_usb_serial_ctrl_irq, xr_usb_serial,
			 /* works around buggy devices */
			 epctrl->bInterval ? epctrl->bInterval : 0xff);
	xr_usb_serial->ctrlurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	xr_usb_serial->ctrlurb->transfer_dma = xr_usb_serial->ctrl_dma;

	dev_info(&intf->dev, "ttyXR_USB_SERIAL%d: USB XR_USB_SERIAL device\n", minor);
	
    xr_usb_serial_pre_setup(xr_usb_serial);
	
	xr_usb_serial_set_control(xr_usb_serial, xr_usb_serial->ctrlout);

	xr_usb_serial->line.dwDTERate = cpu_to_le32(9600);
	xr_usb_serial->line.bDataBits = 8;
	xr_usb_serial_set_line(xr_usb_serial, &xr_usb_serial->line);
    
	usb_driver_claim_interface(&xr_usb_serial_driver, data_interface, xr_usb_serial);
	usb_set_intfdata(data_interface, xr_usb_serial);

	usb_get_intf(control_interface);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0)
    tty_register_device(xr_usb_serial_tty_driver, minor, &control_interface->dev);
#else
	tty_dev = tty_port_register_device(&xr_usb_serial->port, xr_usb_serial_tty_driver, minor,
			&control_interface->dev);
	if (IS_ERR(tty_dev)) {
		rv = PTR_ERR(tty_dev);
		goto alloc_fail8;
	}
#endif	

	return 0;
alloc_fail8:
	if (xr_usb_serial->country_codes) {
		device_remove_file(&xr_usb_serial->control->dev,
				&dev_attr_wCountryCodes);
		device_remove_file(&xr_usb_serial->control->dev,
				&dev_attr_iCountryCodeRelDate);
	}
	device_remove_file(&xr_usb_serial->control->dev, &dev_attr_bmCapabilities);
alloc_fail7:
	usb_set_intfdata(intf, NULL);
	for (i = 0; i < XR_USB_SERIAL_NW; i++)
		usb_free_urb(xr_usb_serial->wb[i].urb);
alloc_fail6:
	for (i = 0; i < num_rx_buf; i++)
		usb_free_urb(xr_usb_serial->read_urbs[i]);
	xr_usb_serial_read_buffers_free(xr_usb_serial);
	usb_free_urb(xr_usb_serial->ctrlurb);
alloc_fail5:
	xr_usb_serial_write_buffers_free(xr_usb_serial);
alloc_fail4:
	usb_free_coherent(usb_dev, ctrlsize, xr_usb_serial->ctrl_buffer, xr_usb_serial->ctrl_dma);
alloc_fail2:
	xr_usb_serial_release_minor(xr_usb_serial);
	kfree(xr_usb_serial);
alloc_fail:
	return rv;
}

static void stop_data_traffic(struct xr_usb_serial *xr_usb_serial)
{
	int i;

	dev_dbg(&xr_usb_serial->control->dev, "%s\n", __func__);

	usb_kill_urb(xr_usb_serial->ctrlurb);
	for (i = 0; i < XR_USB_SERIAL_NW; i++)
		usb_kill_urb(xr_usb_serial->wb[i].urb);
	for (i = 0; i < xr_usb_serial->rx_buflimit; i++)
		usb_kill_urb(xr_usb_serial->read_urbs[i]);

	cancel_work_sync(&xr_usb_serial->work);
}

static void xr_usb_serial_disconnect(struct usb_interface *intf)
{
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct tty_struct *tty;
	int i;

	dev_dbg(&intf->dev, "%s\n", __func__);

	/* sibling interface is already cleaning up */
	if (!xr_usb_serial)
		return;

	mutex_lock(&xr_usb_serial->mutex);
	xr_usb_serial->disconnected = true;
	if (xr_usb_serial->country_codes) {
		device_remove_file(&xr_usb_serial->control->dev,
				&dev_attr_wCountryCodes);
		device_remove_file(&xr_usb_serial->control->dev,
				&dev_attr_iCountryCodeRelDate);
	}
	device_remove_file(&xr_usb_serial->control->dev, &dev_attr_bmCapabilities);
	usb_set_intfdata(xr_usb_serial->control, NULL);
	usb_set_intfdata(xr_usb_serial->data, NULL);
	mutex_unlock(&xr_usb_serial->mutex);

	tty = tty_port_tty_get(&xr_usb_serial->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}
	stop_data_traffic(xr_usb_serial);
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 0)
	tty_unregister_device(xr_usb_serial_tty_driver, xr_usb_serial->minor);
#endif

	usb_free_urb(xr_usb_serial->ctrlurb);
	for (i = 0; i < XR_USB_SERIAL_NW; i++)
		usb_free_urb(xr_usb_serial->wb[i].urb);
	for (i = 0; i < xr_usb_serial->rx_buflimit; i++)
		usb_free_urb(xr_usb_serial->read_urbs[i]);
	xr_usb_serial_write_buffers_free(xr_usb_serial);
	usb_free_coherent(usb_dev, xr_usb_serial->ctrlsize, xr_usb_serial->ctrl_buffer, xr_usb_serial->ctrl_dma);
	xr_usb_serial_read_buffers_free(xr_usb_serial);

	if (!xr_usb_serial->combined_interfaces)
		usb_driver_release_interface(&xr_usb_serial_driver, intf == xr_usb_serial->control ?
					xr_usb_serial->data : xr_usb_serial->control);

	tty_port_put(&xr_usb_serial->port);
}

#ifdef CONFIG_PM
static int xr_usb_serial_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);
	int cnt;

	if (PMSG_IS_AUTO(message)) {
		int b;

		spin_lock_irq(&xr_usb_serial->write_lock);
		b = xr_usb_serial->transmitting;
		spin_unlock_irq(&xr_usb_serial->write_lock);
		if (b)
			return -EBUSY;
	}

	spin_lock_irq(&xr_usb_serial->read_lock);
	spin_lock(&xr_usb_serial->write_lock);
	cnt = xr_usb_serial->susp_count++;
	spin_unlock(&xr_usb_serial->write_lock);
	spin_unlock_irq(&xr_usb_serial->read_lock);

	if (cnt)
		return 0;

	if (test_bit(ASYNCB_INITIALIZED, &xr_usb_serial->port.flags))
		stop_data_traffic(xr_usb_serial);

	return 0;
}

static int xr_usb_serial_resume(struct usb_interface *intf)
{
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);
	struct xr_usb_serial_wb *wb;
	int rv = 0;
	int cnt;

	spin_lock_irq(&xr_usb_serial->read_lock);
	xr_usb_serial->susp_count -= 1;
	cnt = xr_usb_serial->susp_count;
	spin_unlock_irq(&xr_usb_serial->read_lock);

	if (cnt)
		return 0;

	if (test_bit(ASYNCB_INITIALIZED, &xr_usb_serial->port.flags)) {
		rv = usb_submit_urb(xr_usb_serial->ctrlurb, GFP_NOIO);

		spin_lock_irq(&xr_usb_serial->write_lock);
		if (xr_usb_serial->delayed_wb) {
			wb = xr_usb_serial->delayed_wb;
			xr_usb_serial->delayed_wb = NULL;
			spin_unlock_irq(&xr_usb_serial->write_lock);
			xr_usb_serial_start_wb(xr_usb_serial, wb);
		} else {
			spin_unlock_irq(&xr_usb_serial->write_lock);
		}

		/*
		 * delayed error checking because we must
		 * do the write path at all cost
		 */
		if (rv < 0)
			goto err_out;

		rv = xr_usb_serial_submit_read_urbs(xr_usb_serial, GFP_NOIO);
	}

err_out:
	return rv;
}

static int xr_usb_serial_reset_resume(struct usb_interface *intf)
{
	struct xr_usb_serial *xr_usb_serial = usb_get_intfdata(intf);
    struct tty_struct *tty;
	if (test_bit(ASYNCB_INITIALIZED, &xr_usb_serial->port.flags)){
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 9, 0)	
	tty_port_tty_hangup(&xr_usb_serial->port, false);
#else
 	tty = tty_port_tty_get(&xr_usb_serial->port);
	if (tty) {
		tty_hangup(tty);
		tty_kref_put(tty);
	}
#endif
	}
	return xr_usb_serial_resume(intf);
}

#endif /* CONFIG_PM */

/*
 * USB driver structure.
 */
static const struct usb_device_id xr_usb_serial_ids[] = {
	{ USB_DEVICE(0x04e2, 0x1410)},
    { USB_DEVICE(0x04e2, 0x1411)},
	{ USB_DEVICE(0x04e2, 0x1412)},
	{ USB_DEVICE(0x04e2, 0x1414)},
	{ USB_DEVICE(0x04e2, 0x1420)},
    { USB_DEVICE(0x04e2, 0x1421)},
	{ USB_DEVICE(0x04e2, 0x1422)},
	{ USB_DEVICE(0x04e2, 0x1424)},
	{ USB_DEVICE(0x04e2, 0x1400)},
    { USB_DEVICE(0x04e2, 0x1401)},
    { USB_DEVICE(0x04e2, 0x1402)},
    { USB_DEVICE(0x04e2, 0x1403)},
	{ }
};

MODULE_DEVICE_TABLE(usb, xr_usb_serial_ids);

static struct usb_driver xr_usb_serial_driver = {
	.name =		"cdc_xr_usb_serial",
	.probe =	xr_usb_serial_probe,
	.disconnect =	xr_usb_serial_disconnect,
#ifdef CONFIG_PM
	.suspend =	xr_usb_serial_suspend,
	.resume =	xr_usb_serial_resume,
	.reset_resume =	xr_usb_serial_reset_resume,
#endif
	.id_table =	xr_usb_serial_ids,
#ifdef CONFIG_PM
	.supports_autosuspend = 1,
#endif
	.disable_hub_initiated_lpm = 1,
};

/*
 * TTY driver structures.
 */

static const struct tty_operations xr_usb_serial_ops = {
	.install =		xr_usb_serial_tty_install,
	.open =			xr_usb_serial_tty_open,
	.close =		xr_usb_serial_tty_close,
	.cleanup =		xr_usb_serial_tty_cleanup,
	.hangup =		xr_usb_serial_tty_hangup,
	.write =		xr_usb_serial_tty_write,
	.write_room =		xr_usb_serial_tty_write_room,
	.ioctl =		xr_usb_serial_tty_ioctl,
	.throttle =		xr_usb_serial_tty_throttle,
	.unthrottle =		xr_usb_serial_tty_unthrottle,
	.chars_in_buffer =	xr_usb_serial_tty_chars_in_buffer,
	.break_ctl =		xr_usb_serial_tty_break_ctl,
	.set_termios =		xr_usb_serial_tty_set_termios,
	.tiocmget =		xr_usb_serial_tty_tiocmget,
	.tiocmset =		xr_usb_serial_tty_tiocmset,
};

/*
 * Init / exit.
 */

static int __init xr_usb_serial_init(void)
{
	int retval;
	xr_usb_serial_tty_driver = alloc_tty_driver(XR_USB_SERIAL_TTY_MINORS);
	if (!xr_usb_serial_tty_driver)
		return -ENOMEM;
	xr_usb_serial_tty_driver->driver_name = "xr_usb_serial",
	xr_usb_serial_tty_driver->name = "ttyXRUSB",
	xr_usb_serial_tty_driver->major = XR_USB_SERIAL_TTY_MAJOR,
	xr_usb_serial_tty_driver->minor_start = 0,
	xr_usb_serial_tty_driver->type = TTY_DRIVER_TYPE_SERIAL,
	xr_usb_serial_tty_driver->subtype = SERIAL_TYPE_NORMAL,
	xr_usb_serial_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	xr_usb_serial_tty_driver->init_termios = tty_std_termios;
	xr_usb_serial_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD |
								HUPCL | CLOCAL;
	tty_set_operations(xr_usb_serial_tty_driver, &xr_usb_serial_ops);

	retval = tty_register_driver(xr_usb_serial_tty_driver);
	if (retval) {
		put_tty_driver(xr_usb_serial_tty_driver);
		return retval;
	}

	retval = usb_register(&xr_usb_serial_driver);
	if (retval) {
		tty_unregister_driver(xr_usb_serial_tty_driver);
		put_tty_driver(xr_usb_serial_tty_driver);
		return retval;
	}

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_DESC "\n");

	return 0;
}

static void __exit xr_usb_serial_exit(void)
{
	usb_deregister(&xr_usb_serial_driver);
	tty_unregister_driver(xr_usb_serial_tty_driver);
	put_tty_driver(xr_usb_serial_tty_driver);
}

module_init(xr_usb_serial_init);
module_exit(xr_usb_serial_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(XR_USB_SERIAL_TTY_MAJOR);
