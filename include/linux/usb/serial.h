// SPDX-License-Identifier: GPL-2.0
/*
 * USB Serial Converter stuff
 *
 *	Copyright (C) 1999 - 2012
 *	    Greg Kroah-Hartman (greg@kroah.com)
 */

#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/serial.h>
#include <linux/kfifo.h>

/* The maximum number of ports one device can grab at once */
#define MAX_NUM_PORTS		16

/* USB serial flags */
#define USB_SERIAL_WRITE_BUSY	0
#define USB_SERIAL_THROTTLED	1

/**
 * usb_serial_port: structure for the specific ports of a device.
 * @serial: pointer back to the struct usb_serial owner of this port.
 * @port: pointer to the corresponding tty_port for this port.
 * @lock: spinlock to grab when updating portions of this structure.
 * @minor: the minor number of the port
 * @port_number: the struct usb_serial port number of this port (starts at 0)
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
 * @bulk_in_buffers: pointers to the bulk in buffers for this port
 * @read_urbs: pointers to the bulk in urbs for this port
 * @read_urbs_free: status bitmap the for bulk in urbs
 * @bulk_out_buffer: pointer to the bulk out buffer for this port.
 * @bulk_out_size: the size of the bulk_out_buffer, in bytes.
 * @write_urb: pointer to the bulk out struct urb for this port.
 * @write_fifo: kfifo used to buffer outgoing data
 * @bulk_out_buffers: pointers to the bulk out buffers for this port
 * @write_urbs: pointers to the bulk out urbs for this port
 * @write_urbs_free: status bitmap the for bulk out urbs
 * @icount: interrupt counters
 * @tx_bytes: number of bytes currently in host stack queues
 * @bulk_out_endpointAddress: endpoint address for the bulk out pipe for this
 *	port.
 * @flags: usb serial port flags
 * @work: work queue entry for the line discipline waking up.
 * @dev: pointer to the serial device
 *
 * This structure is used by the usb-serial core and drivers for the specific
 * ports of a device.
 */
struct usb_serial_port {
	struct usb_serial	*serial;
	struct tty_port		port;
	spinlock_t		lock;
	u32			minor;
	u8			port_number;

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

	unsigned char		*bulk_in_buffers[2];
	struct urb		*read_urbs[2];
	unsigned long		read_urbs_free;

	unsigned char		*bulk_out_buffer;
	int			bulk_out_size;
	struct urb		*write_urb;
	struct kfifo		write_fifo;

	unsigned char		*bulk_out_buffers[2];
	struct urb		*write_urbs[2];
	unsigned long		write_urbs_free;
	__u8			bulk_out_endpointAddress;

	struct async_icount	icount;
	int			tx_bytes;

	unsigned long		flags;
	struct work_struct	work;
	unsigned long		sysrq; /* sysrq timeout */
	struct device		dev;
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
 * @sibling: pointer to the struct usb_interface of any sibling interface
 * @suspend_count: number of suspended (sibling) interfaces
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
	struct usb_interface		*sibling;
	unsigned int			suspend_count;
	unsigned char			disconnected:1;
	unsigned char			attached:1;
	unsigned char			minors_reserved:1;
	unsigned char			num_ports;
	unsigned char			num_port_pointers;
	unsigned char			num_interrupt_in;
	unsigned char			num_interrupt_out;
	unsigned char			num_bulk_in;
	unsigned char			num_bulk_out;
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

struct usb_serial_endpoints {
	unsigned char num_bulk_in;
	unsigned char num_bulk_out;
	unsigned char num_interrupt_in;
	unsigned char num_interrupt_out;
	struct usb_endpoint_descriptor *bulk_in[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *interrupt_in[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *interrupt_out[MAX_NUM_PORTS];
};

/**
 * usb_serial_driver - describes a usb serial driver
 * @description: pointer to a string that describes this driver.  This string
 *	used in the syslog messages when a device is inserted or removed.
 * @id_table: pointer to a list of usb_device_id structures that define all
 *	of the devices this structure can support.
 * @num_ports: the number of different ports this device will have.
 * @num_bulk_in: minimum number of bulk-in endpoints
 * @num_bulk_out: minimum number of bulk-out endpoints
 * @num_interrupt_in: minimum number of interrupt-in endpoints
 * @num_interrupt_out: minimum number of interrupt-out endpoints
 * @bulk_in_size: minimum number of bytes to allocate for bulk-in buffer
 *	(0 = end-point size)
 * @bulk_out_size: bytes to allocate for bulk-out buffer (0 = end-point size)
 * @calc_num_ports: pointer to a function to determine how many ports this
 *	device has dynamically. It can also be used to verify the number of
 *	endpoints or to modify the port-endpoint mapping. It will be called
 *	after the probe() callback is called, but before attach().
 * @probe: pointer to the driver's probe function.
 *	This will be called when the device is inserted into the system,
 *	but before the device has been fully initialized by the usb_serial
 *	subsystem.  Use this function to download any firmware to the device,
 *	or any other early initialization that might be needed.
 *	Return 0 to continue on with the initialization sequence.  Anything
 *	else will abort it.
 * @attach: pointer to the driver's attach function.
 *	This will be called when the struct usb_serial structure is fully
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

	struct list_head	driver_list;
	struct device_driver	driver;
	struct usb_driver	*usb_driver;
	struct usb_dynids	dynids;

	unsigned char		num_ports;

	unsigned char		num_bulk_in;
	unsigned char		num_bulk_out;
	unsigned char		num_interrupt_in;
	unsigned char		num_interrupt_out;

	size_t			bulk_in_size;
	size_t			bulk_out_size;

	int (*probe)(struct usb_serial *serial, const struct usb_device_id *id);
	int (*attach)(struct usb_serial *serial);
	int (*calc_num_ports)(struct usb_serial *serial,
			struct usb_serial_endpoints *epds);

	void (*disconnect)(struct usb_serial *serial);
	void (*release)(struct usb_serial *serial);

	int (*port_probe)(struct usb_serial_port *port);
	void (*port_remove)(struct usb_serial_port *port);

	int (*suspend)(struct usb_serial *serial, pm_message_t message);
	int (*resume)(struct usb_serial *serial);
	int (*reset_resume)(struct usb_serial *serial);

	/* serial function calls */
	/* Called by console and by the tty layer */
	int  (*open)(struct tty_struct *tty, struct usb_serial_port *port);
	void (*close)(struct usb_serial_port *port);
	int  (*write)(struct tty_struct *tty, struct usb_serial_port *port,
			const unsigned char *buf, int count);
	/* Called only by the tty layer */
	unsigned int (*write_room)(struct tty_struct *tty);
	int  (*ioctl)(struct tty_struct *tty,
		      unsigned int cmd, unsigned long arg);
	void (*get_serial)(struct tty_struct *tty, struct serial_struct *ss);
	int  (*set_serial)(struct tty_struct *tty, struct serial_struct *ss);
	void (*set_termios)(struct tty_struct *tty, struct usb_serial_port *port,
			    const struct ktermios *old);
	void (*break_ctl)(struct tty_struct *tty, int break_state);
	unsigned int (*chars_in_buffer)(struct tty_struct *tty);
	void (*wait_until_sent)(struct tty_struct *tty, long timeout);
	bool (*tx_empty)(struct usb_serial_port *port);
	void (*throttle)(struct tty_struct *tty);
	void (*unthrottle)(struct tty_struct *tty);
	int  (*tiocmget)(struct tty_struct *tty);
	int  (*tiocmset)(struct tty_struct *tty,
			 unsigned int set, unsigned int clear);
	int  (*tiocmiwait)(struct tty_struct *tty, unsigned long arg);
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

int usb_serial_register_drivers(struct usb_serial_driver *const serial_drivers[],
		const char *name, const struct usb_device_id *id_table);
void usb_serial_deregister_drivers(struct usb_serial_driver *const serial_drivers[]);
void usb_serial_port_softint(struct usb_serial_port *port);

int usb_serial_suspend(struct usb_interface *intf, pm_message_t message);
int usb_serial_resume(struct usb_interface *intf);

/* USB Serial console functions */
#ifdef CONFIG_USB_SERIAL_CONSOLE
void usb_serial_console_init(int minor);
void usb_serial_console_exit(void);
void usb_serial_console_disconnect(struct usb_serial *serial);
#else
static inline void usb_serial_console_init(int minor) { }
static inline void usb_serial_console_exit(void) { }
static inline void usb_serial_console_disconnect(struct usb_serial *serial) {}
#endif

/* Functions needed by other parts of the usbserial core */
struct usb_serial_port *usb_serial_port_get_by_minor(unsigned int minor);
void usb_serial_put(struct usb_serial *serial);

int usb_serial_claim_interface(struct usb_serial *serial, struct usb_interface *intf);

int usb_serial_generic_open(struct tty_struct *tty, struct usb_serial_port *port);
int usb_serial_generic_write_start(struct usb_serial_port *port, gfp_t mem_flags);
int usb_serial_generic_write(struct tty_struct *tty, struct usb_serial_port *port,
		const unsigned char *buf, int count);
void usb_serial_generic_close(struct usb_serial_port *port);
int usb_serial_generic_resume(struct usb_serial *serial);
unsigned int usb_serial_generic_write_room(struct tty_struct *tty);
unsigned int usb_serial_generic_chars_in_buffer(struct tty_struct *tty);
void usb_serial_generic_wait_until_sent(struct tty_struct *tty, long timeout);
void usb_serial_generic_read_bulk_callback(struct urb *urb);
void usb_serial_generic_write_bulk_callback(struct urb *urb);
void usb_serial_generic_throttle(struct tty_struct *tty);
void usb_serial_generic_unthrottle(struct tty_struct *tty);
int usb_serial_generic_tiocmiwait(struct tty_struct *tty, unsigned long arg);
int usb_serial_generic_get_icount(struct tty_struct *tty, struct serial_icounter_struct *icount);
int usb_serial_generic_register(void);
void usb_serial_generic_deregister(void);
int usb_serial_generic_submit_read_urbs(struct usb_serial_port *port, gfp_t mem_flags);
void usb_serial_generic_process_read_urb(struct urb *urb);
int usb_serial_generic_prepare_write_buffer(struct usb_serial_port *port, void *dest, size_t size);

#if defined(CONFIG_USB_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
int usb_serial_handle_sysrq_char(struct usb_serial_port *port, unsigned int ch);
int usb_serial_handle_break(struct usb_serial_port *port);
#else
static inline int usb_serial_handle_sysrq_char(struct usb_serial_port *port, unsigned int ch)
{
	return 0;
}
static inline int usb_serial_handle_break(struct usb_serial_port *port)
{
	return 0;
}
#endif

void usb_serial_handle_dcd_change(struct usb_serial_port *usb_port,
		struct tty_struct *tty, unsigned int status);


int usb_serial_bus_register(struct usb_serial_driver *device);
void usb_serial_bus_deregister(struct usb_serial_driver *device);

extern const struct bus_type usb_serial_bus_type;
extern struct tty_driver *usb_serial_tty_driver;

static inline void usb_serial_debug_data(struct device *dev,
					 const char *function, int size,
					 const unsigned char *data)
{
	dev_dbg(dev, "%s - length = %d, data = %*ph\n",
		function, size, size, data);
}

/*
 * Macro for reporting errors in write path to avoid infinite loop
 * when port is used as a console.
 */
#define dev_err_console(usport, fmt, ...)				\
do {									\
	static bool __print_once;					\
	struct usb_serial_port *__port = (usport);			\
									\
	if (!__port->port.console || !__print_once) {			\
		__print_once = true;					\
		dev_err(&__port->dev, fmt, ##__VA_ARGS__);		\
	}								\
} while (0)

/*
 * module_usb_serial_driver() - Helper macro for registering a USB Serial driver
 * @__serial_drivers: list of usb_serial drivers to register
 * @__ids: all device ids that @__serial_drivers bind to
 *
 * Helper macro for USB serial drivers which do not do anything special
 * in module init/exit. This eliminates a lot of boilerplate. Each
 * module may only use this macro once, and calling it replaces
 * module_init() and module_exit()
 *
 */
#define usb_serial_module_driver(__name, __serial_drivers, __ids)	\
static int __init usb_serial_module_init(void)				\
{									\
	return usb_serial_register_drivers(__serial_drivers,		\
					   __name, __ids);		\
}									\
module_init(usb_serial_module_init);					\
static void __exit usb_serial_module_exit(void)				\
{									\
	usb_serial_deregister_drivers(__serial_drivers);		\
}									\
module_exit(usb_serial_module_exit);

#define module_usb_serial_driver(__serial_drivers, __ids)		\
	usb_serial_module_driver(KBUILD_MODNAME, __serial_drivers, __ids)

#endif /* __LINUX_USB_SERIAL_H */

