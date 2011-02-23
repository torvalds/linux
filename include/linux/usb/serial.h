/*
 * USB Serial Converter stuff
 *
 *	Copyright (C) 1999 - 2005
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; version 2 of the License.
 *
 */

#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/sysrq.h>
#include <linux/kfifo.h>

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	254	/* loads of devices :) */
#define SERIAL_TTY_NO_MINOR	255	/* No minor was assigned */

/* The maximum number of ports one device can grab at once */
#define MAX_NUM_PORTS		8

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

enum port_dev_state {
	PORT_UNREGISTERED,
	PORT_REGISTERING,
	PORT_REGISTERED,
	PORT_UNREGISTERING,
};

/* USB serial flags */
#define USB_SERIAL_WRITE_BUSY	0

/**
 * usb_serial_port: structure for the specific ports of a device.
 * @serial: pointer back to the struct usb_serial owner of this port.
 * @port: pointer to the corresponding tty_port for this port.
 * @lock: spinlock to grab when updating portions of this structure.
 * @number: the number of the port (the minor number).
 * @interrupt_in_buffer: pointer to the interrupt in buffer for this port.
 * @interrupt_in_urb: pointer to the interrupt in struct urb for this port.
 * @interrupt_in_endpointAddress: endpoint address for the interrupt in pipe
 *	for this port.
 * @interrupt_out_buffer: pointer to the interrupt out buffer for this port.
 * @interrupt_out_size: the size of the interrupt_out_buffer, in bytes.
 * @interrupt_out_urb: pointer to the interrupt out struct urb for this port.
 * @interrupt_out_endpointAddress: endpoint address for the interrupt out pipe
 *	for this port.
 * @bulk_in_buffer: pointer to the bulk in buffer for this port.
 * @bulk_in_size: the size of the bulk_in_buffer, in bytes.
 * @read_urb: pointer to the bulk in struct urb for this port.
 * @bulk_in_endpointAddress: endpoint address for the bulk in pipe for this
 *	port.
 * @bulk_out_buffer: pointer to the bulk out buffer for this port.
 * @bulk_out_size: the size of the bulk_out_buffer, in bytes.
 * @write_urb: pointer to the bulk out struct urb for this port.
 * @write_fifo: kfifo used to buffer outgoing data
 * @write_urb_busy: port`s writing status
 * @bulk_out_buffers: pointers to the bulk out buffers for this port
 * @write_urbs: pointers to the bulk out urbs for this port
 * @write_urbs_free: status bitmap the for bulk out urbs
 * @tx_bytes: number of bytes currently in host stack queues
 * @bulk_out_endpointAddress: endpoint address for the bulk out pipe for this
 *	port.
 * @flags: usb serial port flags
 * @write_wait: a wait_queue_head_t used by the port.
 * @work: work queue entry for the line discipline waking up.
 * @throttled: nonzero if the read urb is inactive to throttle the device
 * @throttle_req: nonzero if the tty wants to throttle us
 * @dev: pointer to the serial device
 *
 * This structure is used by the usb-serial core and drivers for the specific
 * ports of a device.
 */
struct usb_serial_port {
	struct usb_serial	*serial;
	struct tty_port		port;
	spinlock_t		lock;
	unsigned char		number;

	unsigned char		*interrupt_in_buffer;
	struct urb		*interrupt_in_urb;
	__u8			interrupt_in_endpointAddress;

	unsigned char		*interrupt_out_buffer;
	int			interrupt_out_size;
	struct urb		*interrupt_out_urb;
	__u8			interrupt_out_endpointAddress;

	unsigned char		*bulk_in_buffer;
	int			bulk_in_size;
	struct urb		*read_urb;
	__u8			bulk_in_endpointAddress;

	unsigned char		*bulk_out_buffer;
	int			bulk_out_size;
	struct urb		*write_urb;
	struct kfifo		write_fifo;
	int			write_urb_busy;

	unsigned char		*bulk_out_buffers[2];
	struct urb		*write_urbs[2];
	unsigned long		write_urbs_free;
	__u8			bulk_out_endpointAddress;

	int			tx_bytes;

	unsigned long		flags;
	wait_queue_head_t	write_wait;
	struct work_struct	work;
	char			throttled;
	char			throttle_req;
	unsigned long		sysrq; /* sysrq timeout */
	struct device		dev;
	enum port_dev_state	dev_state;
};
#define to_usb_serial_port(d) container_of(d, struct usb_serial_port, dev)

/* get and set the port private data pointer helper functions */
static inline void *usb_get_serial_port_data(struct usb_serial_port *port)
{
	return dev_get_drvdata(&port->dev);
}

static inline void usb_set_serial_port_data(struct usb_serial_port *port,
					    void *data)
{
	dev_set_drvdata(&port->dev, data);
}

/**
 * usb_serial - structure used by the usb-serial core for a device
 * @dev: pointer to the struct usb_device for this device
 * @type: pointer to the struct usb_serial_driver for this device
 * @interface: pointer to the struct usb_interface for this device
 * @minor: the starting minor number for this device
 * @num_ports: the number of ports this device has
 * @num_interrupt_in: number of interrupt in endpoints we have
 * @num_interrupt_out: number of interrupt out endpoints we have
 * @num_bulk_in: number of bulk in endpoints we have
 * @num_bulk_out: number of bulk out endpoints we have
 * @port: array of struct usb_serial_port structures for the different ports.
 * @private: place to put any driver specific information that is needed.  The
 *	usb-serial driver is required to manage this data, the usb-serial core
 *	will not touch this.  Use usb_get_serial_data() and
 *	usb_set_serial_data() to access this.
 */
struct usb_serial {
	struct usb_device		*dev;
	struct usb_serial_driver	*type;
	struct usb_interface		*interface;
	unsigned char			disconnected:1;
	unsigned char			suspending:1;
	unsigned char			attached:1;
	unsigned char			minor;
	unsigned char			num_ports;
	unsigned char			num_port_pointers;
	char				num_interrupt_in;
	char				num_interrupt_out;
	char				num_bulk_in;
	char				num_bulk_out;
	struct usb_serial_port		*port[MAX_NUM_PORTS];
	struct kref			kref;
	struct mutex			disc_mutex;
	void				*private;
};
#define to_usb_serial(d) container_of(d, struct usb_serial, kref)

/* get and set the serial private data pointer helper functions */
static inline void *usb_get_serial_data(struct usb_serial *serial)
{
	return serial->private;
}

static inline void usb_set_serial_data(struct usb_serial *serial, void *data)
{
	serial->private = data;
}

/**
 * usb_serial_driver - describes a usb serial driver
 * @description: pointer to a string that describes this driver.  This string
 *	used in the syslog messages when a device is inserted or removed.
 * @id_table: pointer to a list of usb_device_id structures that define all
 *	of the devices this structure can support.
 * @num_ports: the number of different ports this device will have.
 * @bulk_in_size: minimum number of bytes to allocate for bulk-in buffer
 *	(0 = end-point size)
 * @bulk_out_size: bytes to allocate for bulk-out buffer (0 = end-point size)
 * @calc_num_ports: pointer to a function to determine how many ports this
 *	device has dynamically.  It will be called after the probe()
 *	callback is called, but before attach()
 * @probe: pointer to the driver's probe function.
 *	This will be called when the device is inserted into the system,
 *	but before the device has been fully initialized by the usb_serial
 *	subsystem.  Use this function to download any firmware to the device,
 *	or any other early initialization that might be needed.
 *	Return 0 to continue on with the initialization sequence.  Anything
 *	else will abort it.
 * @attach: pointer to the driver's attach function.
 *	This will be called when the struct usb_serial structure is fully set
 *	set up.  Do any local initialization of the device, or any private
 *	memory structure allocation at this point in time.
 * @disconnect: pointer to the driver's disconnect function.  This will be
 *	called when the device is unplugged or unbound from the driver.
 * @release: pointer to the driver's release function.  This will be called
 *	when the usb_serial data structure is about to be destroyed.
 * @usb_driver: pointer to the struct usb_driver that controls this
 *	device.  This is necessary to allow dynamic ids to be added to
 *	the driver from sysfs.
 *
 * This structure is defines a USB Serial driver.  It provides all of
 * the information that the USB serial core code needs.  If the function
 * pointers are defined, then the USB serial core code will call them when
 * the corresponding tty port functions are called.  If they are not
 * called, the generic serial function will be used instead.
 *
 * The driver.owner field should be set to the module owner of this driver.
 * The driver.name field should be set to the name of this driver (remember
 * it will show up in sysfs, so it needs to be short and to the point.
 * Using the module name is a good idea.)
 */
struct usb_serial_driver {
	const char *description;
	const struct usb_device_id *id_table;
	char	num_ports;

	struct list_head	driver_list;
	struct device_driver	driver;
	struct usb_driver	*usb_driver;
	struct usb_dynids	dynids;

	size_t			bulk_in_size;
	size_t			bulk_out_size;

	int (*probe)(struct usb_serial *serial, const struct usb_device_id *id);
	int (*attach)(struct usb_serial *serial);
	int (*calc_num_ports) (struct usb_serial *serial);

	void (*disconnect)(struct usb_serial *serial);
	void (*release)(struct usb_serial *serial);

	int (*port_probe)(struct usb_serial_port *port);
	int (*port_remove)(struct usb_serial_port *port);

	int (*suspend)(struct usb_serial *serial, pm_message_t message);
	int (*resume)(struct usb_serial *serial);

	/* serial function calls */
	/* Called by console and by the tty layer */
	int  (*open)(struct tty_struct *tty, struct usb_serial_port *port);
	void (*close)(struct usb_serial_port *port);
	int  (*write)(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count);
	/* Called only by the tty layer */
	int  (*write_room)(struct tty_struct *tty);
	int  (*ioctl)(struct tty_struct *tty, struct file *file,
		      unsigned int cmd, unsigned long arg);
	void (*set_termios)(struct tty_struct *tty,
			struct usb_serial_port *port, struct ktermios *old);
	void (*break_ctl)(struct tty_struct *tty, int break_state);
	int  (*chars_in_buffer)(struct tty_struct *tty);
	void (*throttle)(struct tty_struct *tty);
	void (*unthrottle)(struct tty_struct *tty);
	int  (*tiocmget)(struct tty_struct *tty, struct file *file);
	int  (*tiocmset)(struct tty_struct *tty, struct file *file,
			 unsigned int set, unsigned int clear);
	int  (*get_icount)(struct tty_struct *tty,
			struct serial_icounter_struct *icount);
	/* Called by the tty layer for port level work. There may or may not
	   be an attached tty at this point */
	void (*dtr_rts)(struct usb_serial_port *port, int on);
	int  (*carrier_raised)(struct usb_serial_port *port);
	/* Called by the usb serial hooks to allow the user to rework the
	   termios state */
	void (*init_termios)(struct tty_struct *tty);
	/* USB events */
	void (*read_int_callback)(struct urb *urb);
	void (*write_int_callback)(struct urb *urb);
	void (*read_bulk_callback)(struct urb *urb);
	void (*write_bulk_callback)(struct urb *urb);
	/* Called by the generic read bulk callback */
	void (*process_read_urb)(struct urb *urb);
	/* Called by the generic write implementation */
	int (*prepare_write_buffer)(struct usb_serial_port *port,
						void *dest, size_t size);
};
#define to_usb_serial_driver(d) \
	container_of(d, struct usb_serial_driver, driver)

extern int  usb_serial_register(struct usb_serial_driver *driver);
extern void usb_serial_deregister(struct usb_serial_driver *driver);
extern void usb_serial_port_softint(struct usb_serial_port *port);

extern int usb_serial_probe(struct usb_interface *iface,
			    const struct usb_device_id *id);
extern void usb_serial_disconnect(struct usb_interface *iface);

extern int usb_serial_suspend(struct usb_interface *intf, pm_message_t message);
extern int usb_serial_resume(struct usb_interface *intf);

extern int ezusb_writememory(struct usb_serial *serial, int address,
			     unsigned char *data, int length, __u8 bRequest);
extern int ezusb_set_reset(struct usb_serial *serial, unsigned char reset_bit);

/* USB Serial console functions */
#ifdef CONFIG_USB_SERIAL_CONSOLE
extern void usb_serial_console_init(int debug, int minor);
extern void usb_serial_console_exit(void);
extern void usb_serial_console_disconnect(struct usb_serial *serial);
#else
static inline void usb_serial_console_init(int debug, int minor) { }
static inline void usb_serial_console_exit(void) { }
static inline void usb_serial_console_disconnect(struct usb_serial *serial) {}
#endif

/* Functions needed by other parts of the usbserial core */
extern struct usb_serial *usb_serial_get_by_index(unsigned int minor);
extern void usb_serial_put(struct usb_serial *serial);
extern int usb_serial_generic_open(struct tty_struct *tty,
	struct usb_serial_port *port);
extern int usb_serial_generic_write(struct tty_struct *tty,
	struct usb_serial_port *port, const unsigned char *buf, int count);
extern void usb_serial_generic_close(struct usb_serial_port *port);
extern int usb_serial_generic_resume(struct usb_serial *serial);
extern int usb_serial_generic_write_room(struct tty_struct *tty);
extern int usb_serial_generic_chars_in_buffer(struct tty_struct *tty);
extern void usb_serial_generic_read_bulk_callback(struct urb *urb);
extern void usb_serial_generic_write_bulk_callback(struct urb *urb);
extern void usb_serial_generic_throttle(struct tty_struct *tty);
extern void usb_serial_generic_unthrottle(struct tty_struct *tty);
extern void usb_serial_generic_disconnect(struct usb_serial *serial);
extern void usb_serial_generic_release(struct usb_serial *serial);
extern int usb_serial_generic_register(int debug);
extern void usb_serial_generic_deregister(void);
extern int usb_serial_generic_submit_read_urb(struct usb_serial_port *port,
						 gfp_t mem_flags);
extern void usb_serial_generic_process_read_urb(struct urb *urb);
extern int usb_serial_generic_prepare_write_buffer(struct usb_serial_port *port,
						void *dest, size_t size);
extern int usb_serial_handle_sysrq_char(struct usb_serial_port *port,
					unsigned int ch);
extern int usb_serial_handle_break(struct usb_serial_port *port);
extern void usb_serial_handle_dcd_change(struct usb_serial_port *usb_port,
					 struct tty_struct *tty,
					 unsigned int status);


extern int usb_serial_bus_register(struct usb_serial_driver *device);
extern void usb_serial_bus_deregister(struct usb_serial_driver *device);

extern struct usb_serial_driver usb_serial_generic_device;
extern struct bus_type usb_serial_bus_type;
extern struct tty_driver *usb_serial_tty_driver;

static inline void usb_serial_debug_data(int debug,
					 struct device *dev,
					 const char *function, int size,
					 const unsigned char *data)
{
	int i;

	if (debug) {
		dev_printk(KERN_DEBUG, dev, "%s - length = %d, data = ",
			   function, size);
		for (i = 0; i < size; ++i)
			printk("%.2x ", data[i]);
		printk("\n");
	}
}

/* Use our own dbg macro */
#undef dbg
#define dbg(format, arg...)						\
do {									\
	if (debug)							\
		printk(KERN_DEBUG "%s: " format "\n", __FILE__, ##arg);	\
} while (0)

#endif /* __LINUX_USB_SERIAL_H */

