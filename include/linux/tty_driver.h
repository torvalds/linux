/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_DRIVER_H
#define _LINUX_TTY_DRIVER_H

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/termios.h>
#include <linux/seq_file.h>

struct tty_struct;
struct tty_driver;
struct serial_icounter_struct;
struct serial_struct;

/**
 * struct tty_operations -- interface between driver and tty
 *
 * @lookup: ``struct tty_struct *()(struct tty_driver *self, struct file *,
 *				    int idx)``
 *
 *	Return the tty device corresponding to @idx, %NULL if there is not
 *	one currently in use and an %ERR_PTR value on error. Called under
 *	%tty_mutex (for now!)
 *
 *	Optional method. Default behaviour is to use the @self->ttys array.
 *
 * @install: ``int ()(struct tty_driver *self, struct tty_struct *tty)``
 *
 *	Install a new @tty into the @self's internal tables. Used in
 *	conjunction with @lookup and @remove methods.
 *
 *	Optional method. Default behaviour is to use the @self->ttys array.
 *
 * @remove: ``void ()(struct tty_driver *self, struct tty_struct *tty)``
 *
 *	Remove a closed @tty from the @self's internal tables. Used in
 *	conjunction with @lookup and @remove methods.
 *
 *	Optional method. Default behaviour is to use the @self->ttys array.
 *
 * @open: ``int ()(struct tty_struct *tty, struct file *)``
 *
 *	This routine is called when a particular @tty device is opened. This
 *	routine is mandatory; if this routine is not filled in, the attempted
 *	open will fail with %ENODEV.
 *
 *	Required method. Called with tty lock held. May sleep.
 *
 * @close: ``void ()(struct tty_struct *tty, struct file *)``
 *
 *	This routine is called when a particular @tty device is closed. At the
 *	point of return from this call the driver must make no further ldisc
 *	calls of any kind.
 *
 *	Remark: called even if the corresponding @open() failed.
 *
 *	Required method. Called with tty lock held. May sleep.
 *
 * @shutdown: ``void ()(struct tty_struct *tty)``
 *
 *	This routine is called under the tty lock when a particular @tty device
 *	is closed for the last time. It executes before the @tty resources
 *	are freed so may execute while another function holds a @tty kref.
 *
 * @cleanup: ``void ()(struct tty_struct *tty)``
 *
 *	This routine is called asynchronously when a particular @tty device
 *	is closed for the last time freeing up the resources. This is
 *	actually the second part of shutdown for routines that might sleep.
 *
 * @write: ``ssize_t ()(struct tty_struct *tty, const u8 *buf, size_t count)``
 *
 *	This routine is called by the kernel to write a series (@count) of
 *	characters (@buf) to the @tty device. The characters may come from
 *	user space or kernel space.  This routine will return the
 *	number of characters actually accepted for writing.
 *
 *	May occur in parallel in special cases. Because this includes panic
 *	paths drivers generally shouldn't try and do clever locking here.
 *
 *	Optional: Required for writable devices. May not sleep.
 *
 * @put_char: ``int ()(struct tty_struct *tty, u8 ch)``
 *
 *	This routine is called by the kernel to write a single character @ch to
 *	the @tty device. If the kernel uses this routine, it must call the
 *	@flush_chars() routine (if defined) when it is done stuffing characters
 *	into the driver. If there is no room in the queue, the character is
 *	ignored.
 *
 *	Optional: Kernel will use the @write method if not provided. Do not
 *	call this function directly, call tty_put_char().
 *
 * @flush_chars: ``void ()(struct tty_struct *tty)``
 *
 *	This routine is called by the kernel after it has written a
 *	series of characters to the tty device using @put_char().
 *
 *	Optional. Do not call this function directly, call
 *	tty_driver_flush_chars().
 *
 * @write_room: ``unsigned int ()(struct tty_struct *tty)``
 *
 *	This routine returns the numbers of characters the @tty driver
 *	will accept for queuing to be written.  This number is subject
 *	to change as output buffers get emptied, or if the output flow
 *	control is acted.
 *
 *	The ldisc is responsible for being intelligent about multi-threading of
 *	write_room/write calls
 *
 *	Required if @write method is provided else not needed. Do not call this
 *	function directly, call tty_write_room()
 *
 * @chars_in_buffer: ``unsigned int ()(struct tty_struct *tty)``
 *
 *	This routine returns the number of characters in the device private
 *	output queue. Used in tty_wait_until_sent() and for poll()
 *	implementation.
 *
 *	Optional: if not provided, it is assumed there is no queue on the
 *	device. Do not call this function directly, call tty_chars_in_buffer().
 *
 * @ioctl: ``int ()(struct tty_struct *tty, unsigned int cmd,
 *		    unsigned long arg)``
 *
 *	This routine allows the @tty driver to implement device-specific
 *	ioctls. If the ioctl number passed in @cmd is not recognized by the
 *	driver, it should return %ENOIOCTLCMD.
 *
 *	Optional.
 *
 * @compat_ioctl: ``long ()(struct tty_struct *tty, unsigned int cmd,
 *			  unsigned long arg)``
 *
 *	Implement ioctl processing for 32 bit process on 64 bit system.
 *
 *	Optional.
 *
 * @set_termios: ``void ()(struct tty_struct *tty, const struct ktermios *old)``
 *
 *	This routine allows the @tty driver to be notified when device's
 *	termios settings have changed. New settings are in @tty->termios.
 *	Previous settings are passed in the @old argument.
 *
 *	The API is defined such that the driver should return the actual modes
 *	selected. This means that the driver is responsible for modifying any
 *	bits in @tty->termios it cannot fulfill to indicate the actual modes
 *	being used.
 *
 *	Optional. Called under the @tty->termios_rwsem. May sleep.
 *
 * @set_ldisc: ``void ()(struct tty_struct *tty)``
 *
 *	This routine allows the @tty driver to be notified when the device's
 *	line discipline is being changed. At the point this is done the
 *	discipline is not yet usable.
 *
 *	Optional. Called under the @tty->ldisc_sem and @tty->termios_rwsem.
 *
 * @throttle: ``void ()(struct tty_struct *tty)``
 *
 *	This routine notifies the @tty driver that input buffers for the line
 *	discipline are close to full, and it should somehow signal that no more
 *	characters should be sent to the @tty.
 *
 *	Serialization including with @unthrottle() is the job of the ldisc
 *	layer.
 *
 *	Optional: Always invoke via tty_throttle_safe(). Called under the
 *	@tty->termios_rwsem.
 *
 * @unthrottle: ``void ()(struct tty_struct *tty)``
 *
 *	This routine notifies the @tty driver that it should signal that
 *	characters can now be sent to the @tty without fear of overrunning the
 *	input buffers of the line disciplines.
 *
 *	Optional. Always invoke via tty_unthrottle(). Called under the
 *	@tty->termios_rwsem.
 *
 * @stop: ``void ()(struct tty_struct *tty)``
 *
 *	This routine notifies the @tty driver that it should stop outputting
 *	characters to the tty device.
 *
 *	Called with @tty->flow.lock held. Serialized with @start() method.
 *
 *	Optional. Always invoke via stop_tty().
 *
 * @start: ``void ()(struct tty_struct *tty)``
 *
 *	This routine notifies the @tty driver that it resumed sending
 *	characters to the @tty device.
 *
 *	Called with @tty->flow.lock held. Serialized with stop() method.
 *
 *	Optional. Always invoke via start_tty().
 *
 * @hangup: ``void ()(struct tty_struct *tty)``
 *
 *	This routine notifies the @tty driver that it should hang up the @tty
 *	device.
 *
 *	Optional. Called with tty lock held.
 *
 * @break_ctl: ``int ()(struct tty_struct *tty, int state)``
 *
 *	This optional routine requests the @tty driver to turn on or off BREAK
 *	status on the RS-232 port. If @state is -1, then the BREAK status
 *	should be turned on; if @state is 0, then BREAK should be turned off.
 *
 *	If this routine is implemented, the high-level tty driver will handle
 *	the following ioctls: %TCSBRK, %TCSBRKP, %TIOCSBRK, %TIOCCBRK.
 *
 *	If the driver sets %TTY_DRIVER_HARDWARE_BREAK in tty_alloc_driver(),
 *	then the interface will also be called with actual times and the
 *	hardware is expected to do the delay work itself. 0 and -1 are still
 *	used for on/off.
 *
 *	Optional: Required for %TCSBRK/%BRKP/etc. handling. May sleep.
 *
 * @flush_buffer: ``void ()(struct tty_struct *tty)``
 *
 *	This routine discards device private output buffer. Invoked on close,
 *	hangup, to implement %TCOFLUSH ioctl and similar.
 *
 *	Optional: if not provided, it is assumed there is no queue on the
 *	device. Do not call this function directly, call
 *	tty_driver_flush_buffer().
 *
 * @wait_until_sent: ``void ()(struct tty_struct *tty, int timeout)``
 *
 *	This routine waits until the device has written out all of the
 *	characters in its transmitter FIFO. Or until @timeout (in jiffies) is
 *	reached.
 *
 *	Optional: If not provided, the device is assumed to have no FIFO.
 *	Usually correct to invoke via tty_wait_until_sent(). May sleep.
 *
 * @send_xchar: ``void ()(struct tty_struct *tty, u8 ch)``
 *
 *	This routine is used to send a high-priority XON/XOFF character (@ch)
 *	to the @tty device.
 *
 *	Optional: If not provided, then the @write method is called under
 *	the @tty->atomic_write_lock to keep it serialized with the ldisc.
 *
 * @tiocmget: ``int ()(struct tty_struct *tty)``
 *
 *	This routine is used to obtain the modem status bits from the @tty
 *	driver.
 *
 *	Optional: If not provided, then %ENOTTY is returned from the %TIOCMGET
 *	ioctl. Do not call this function directly, call tty_tiocmget().
 *
 * @tiocmset: ``int ()(struct tty_struct *tty,
 *		       unsigned int set, unsigned int clear)``
 *
 *	This routine is used to set the modem status bits to the @tty driver.
 *	First, @clear bits should be cleared, then @set bits set.
 *
 *	Optional: If not provided, then %ENOTTY is returned from the %TIOCMSET
 *	ioctl. Do not call this function directly, call tty_tiocmset().
 *
 * @resize: ``int ()(struct tty_struct *tty, struct winsize *ws)``
 *
 *	Called when a termios request is issued which changes the requested
 *	terminal geometry to @ws.
 *
 *	Optional: the default action is to update the termios structure
 *	without error. This is usually the correct behaviour. Drivers should
 *	not force errors here if they are not resizable objects (e.g. a serial
 *	line). See tty_do_resize() if you need to wrap the standard method
 *	in your own logic -- the usual case.
 *
 * @get_icount: ``int ()(struct tty_struct *tty,
 *			 struct serial_icounter *icount)``
 *
 *	Called when the @tty device receives a %TIOCGICOUNT ioctl. Passed a
 *	kernel structure @icount to complete.
 *
 *	Optional: called only if provided, otherwise %ENOTTY will be returned.
 *
 * @get_serial: ``int ()(struct tty_struct *tty, struct serial_struct *p)``
 *
 *	Called when the @tty device receives a %TIOCGSERIAL ioctl. Passed a
 *	kernel structure @p (&struct serial_struct) to complete.
 *
 *	Optional: called only if provided, otherwise %ENOTTY will be returned.
 *	Do not call this function directly, call tty_tiocgserial().
 *
 * @set_serial: ``int ()(struct tty_struct *tty, struct serial_struct *p)``
 *
 *	Called when the @tty device receives a %TIOCSSERIAL ioctl. Passed a
 *	kernel structure @p (&struct serial_struct) to set the values from.
 *
 *	Optional: called only if provided, otherwise %ENOTTY will be returned.
 *	Do not call this function directly, call tty_tiocsserial().
 *
 * @show_fdinfo: ``void ()(struct tty_struct *tty, struct seq_file *m)``
 *
 *	Called when the @tty device file descriptor receives a fdinfo request
 *	from VFS (to show in /proc/<pid>/fdinfo/). @m should be filled with
 *	information.
 *
 *	Optional: called only if provided, otherwise nothing is written to @m.
 *	Do not call this function directly, call tty_show_fdinfo().
 *
 * @poll_init: ``int ()(struct tty_driver *driver, int line, char *options)``
 *
 *	kgdboc support (Documentation/dev-tools/kgdb.rst). This routine is
 *	called to initialize the HW for later use by calling @poll_get_char or
 *	@poll_put_char.
 *
 *	Optional: called only if provided, otherwise skipped as a non-polling
 *	driver.
 *
 * @poll_get_char: ``int ()(struct tty_driver *driver, int line)``
 *
 *	kgdboc support (see @poll_init). @driver should read a character from a
 *	tty identified by @line and return it.
 *
 *	Optional: called only if @poll_init provided.
 *
 * @poll_put_char: ``void ()(struct tty_driver *driver, int line, char ch)``
 *
 *	kgdboc support (see @poll_init). @driver should write character @ch to
 *	a tty identified by @line.
 *
 *	Optional: called only if @poll_init provided.
 *
 * @proc_show: ``int ()(struct seq_file *m, void *driver)``
 *
 *	Driver @driver (cast to &struct tty_driver) can show additional info in
 *	/proc/tty/driver/<driver_name>. It is enough to fill in the information
 *	into @m.
 *
 *	Optional: called only if provided, otherwise no /proc entry created.
 *
 * This structure defines the interface between the low-level tty driver and
 * the tty routines. These routines can be defined. Unless noted otherwise,
 * they are optional, and can be filled in with a %NULL pointer.
 */
struct tty_operations {
	struct tty_struct * (*lookup)(struct tty_driver *driver,
			struct file *filp, int idx);
	int  (*install)(struct tty_driver *driver, struct tty_struct *tty);
	void (*remove)(struct tty_driver *driver, struct tty_struct *tty);
	int  (*open)(struct tty_struct * tty, struct file * filp);
	void (*close)(struct tty_struct * tty, struct file * filp);
	void (*shutdown)(struct tty_struct *tty);
	void (*cleanup)(struct tty_struct *tty);
	ssize_t (*write)(struct tty_struct *tty, const u8 *buf, size_t count);
	int  (*put_char)(struct tty_struct *tty, u8 ch);
	void (*flush_chars)(struct tty_struct *tty);
	unsigned int (*write_room)(struct tty_struct *tty);
	unsigned int (*chars_in_buffer)(struct tty_struct *tty);
	int  (*ioctl)(struct tty_struct *tty,
		    unsigned int cmd, unsigned long arg);
	long (*compat_ioctl)(struct tty_struct *tty,
			     unsigned int cmd, unsigned long arg);
	void (*set_termios)(struct tty_struct *tty, const struct ktermios *old);
	void (*throttle)(struct tty_struct * tty);
	void (*unthrottle)(struct tty_struct * tty);
	void (*stop)(struct tty_struct *tty);
	void (*start)(struct tty_struct *tty);
	void (*hangup)(struct tty_struct *tty);
	int (*break_ctl)(struct tty_struct *tty, int state);
	void (*flush_buffer)(struct tty_struct *tty);
	void (*set_ldisc)(struct tty_struct *tty);
	void (*wait_until_sent)(struct tty_struct *tty, int timeout);
	void (*send_xchar)(struct tty_struct *tty, u8 ch);
	int (*tiocmget)(struct tty_struct *tty);
	int (*tiocmset)(struct tty_struct *tty,
			unsigned int set, unsigned int clear);
	int (*resize)(struct tty_struct *tty, struct winsize *ws);
	int (*get_icount)(struct tty_struct *tty,
				struct serial_icounter_struct *icount);
	int  (*get_serial)(struct tty_struct *tty, struct serial_struct *p);
	int  (*set_serial)(struct tty_struct *tty, struct serial_struct *p);
	void (*show_fdinfo)(struct tty_struct *tty, struct seq_file *m);
#ifdef CONFIG_CONSOLE_POLL
	int (*poll_init)(struct tty_driver *driver, int line, char *options);
	int (*poll_get_char)(struct tty_driver *driver, int line);
	void (*poll_put_char)(struct tty_driver *driver, int line, char ch);
#endif
	int (*proc_show)(struct seq_file *m, void *driver);
} __randomize_layout;

/**
 * struct tty_driver -- driver for TTY devices
 *
 * @kref: reference counting. Reaching zero frees all the internals and the
 *	  driver.
 * @cdevs: allocated/registered character /dev devices
 * @owner: modules owning this driver. Used drivers cannot be rmmod'ed.
 *	   Automatically set by tty_alloc_driver().
 * @driver_name: name of the driver used in /proc/tty
 * @name: used for constructing /dev node name
 * @name_base: used as a number base for constructing /dev node name
 * @major: major /dev device number (zero for autoassignment)
 * @minor_start: the first minor /dev device number
 * @num: number of devices allocated
 * @type: type of tty driver (%TTY_DRIVER_TYPE_)
 * @subtype: subtype of tty driver (%SYSTEM_TYPE_, %PTY_TYPE_, %SERIAL_TYPE_)
 * @init_termios: termios to set to each tty initially (e.g. %tty_std_termios)
 * @flags: tty driver flags (%TTY_DRIVER_)
 * @proc_entry: proc fs entry, used internally
 * @other: driver of the linked tty; only used for the PTY driver
 * @ttys: array of active &struct tty_struct, set by tty_standard_install()
 * @ports: array of &struct tty_port; can be set during initialization by
 *	   tty_port_link_device() and similar
 * @termios: storage for termios at each TTY close for the next open
 * @driver_state: pointer to driver's arbitrary data
 * @ops: driver hooks for TTYs. Set them using tty_set_operations(). Use &struct
 *	 tty_port helpers in them as much as possible.
 * @tty_drivers: used internally to link tty_drivers together
 *
 * The usual handling of &struct tty_driver is to allocate it by
 * tty_alloc_driver(), set up all the necessary members, and register it by
 * tty_register_driver(). At last, the driver is torn down by calling
 * tty_unregister_driver() followed by tty_driver_kref_put().
 *
 * The fields required to be set before calling tty_register_driver() include
 * @driver_name, @name, @type, @subtype, @init_termios, and @ops.
 */
struct tty_driver {
	struct kref kref;
	struct cdev **cdevs;
	struct module	*owner;
	const char	*driver_name;
	const char	*name;
	int	name_base;
	int	major;
	int	minor_start;
	unsigned int	num;
	short	type;
	short	subtype;
	struct ktermios init_termios;
	unsigned long	flags;
	struct proc_dir_entry *proc_entry;
	struct tty_driver *other;

	/*
	 * Pointer to the tty data structures
	 */
	struct tty_struct **ttys;
	struct tty_port **ports;
	struct ktermios **termios;
	void *driver_state;

	/*
	 * Driver methods
	 */

	const struct tty_operations *ops;
	struct list_head tty_drivers;
} __randomize_layout;

extern struct list_head tty_drivers;

struct tty_driver *__tty_alloc_driver(unsigned int lines, struct module *owner,
		unsigned long flags);
struct tty_driver *tty_find_polling_driver(char *name, int *line);

void tty_driver_kref_put(struct tty_driver *driver);

/* Use TTY_DRIVER_* flags below */
#define tty_alloc_driver(lines, flags) \
		__tty_alloc_driver(lines, THIS_MODULE, flags)

static inline struct tty_driver *tty_driver_kref_get(struct tty_driver *d)
{
	kref_get(&d->kref);
	return d;
}

static inline void tty_set_operations(struct tty_driver *driver,
		const struct tty_operations *op)
{
	driver->ops = op;
}

/**
 * DOC: TTY Driver Flags
 *
 * TTY_DRIVER_RESET_TERMIOS
 *	Requests the tty layer to reset the termios setting when the last
 *	process has closed the device. Used for PTYs, in particular.
 *
 * TTY_DRIVER_REAL_RAW
 *	Indicates that the driver will guarantee not to set any special
 *	character handling flags if this is set for the tty:
 *
 *	``(IGNBRK || (!BRKINT && !PARMRK)) && (IGNPAR || !INPCK)``
 *
 *	That is, if there is no reason for the driver to
 *	send notifications of parity and break characters up to the line
 *	driver, it won't do so.  This allows the line driver to optimize for
 *	this case if this flag is set.  (Note that there is also a promise, if
 *	the above case is true, not to signal overruns, either.)
 *
 * TTY_DRIVER_DYNAMIC_DEV
 *	The individual tty devices need to be registered with a call to
 *	tty_register_device() when the device is found in the system and
 *	unregistered with a call to tty_unregister_device() so the devices will
 *	be show up properly in sysfs.  If not set, all &tty_driver.num entries
 *	will be created by the tty core in sysfs when tty_register_driver() is
 *	called.  This is to be used by drivers that have tty devices that can
 *	appear and disappear while the main tty driver is registered with the
 *	tty core.
 *
 * TTY_DRIVER_DEVPTS_MEM
 *	Don't use the standard arrays (&tty_driver.ttys and
 *	&tty_driver.termios), instead use dynamic memory keyed through the
 *	devpts filesystem. This is only applicable to the PTY driver.
 *
 * TTY_DRIVER_HARDWARE_BREAK
 *	Hardware handles break signals. Pass the requested timeout to the
 *	&tty_operations.break_ctl instead of using a simple on/off interface.
 *
 * TTY_DRIVER_DYNAMIC_ALLOC
 *	Do not allocate structures which are needed per line for this driver
 *	(&tty_driver.ports) as it would waste memory. The driver will take
 *	care. This is only applicable to the PTY driver.
 *
 * TTY_DRIVER_UNNUMBERED_NODE
 *	Do not create numbered ``/dev`` nodes. For example, create
 *	``/dev/ttyprintk`` and not ``/dev/ttyprintk0``. Applicable only when a
 *	driver for a single tty device is being allocated.
 */
#define TTY_DRIVER_INSTALLED		0x0001
#define TTY_DRIVER_RESET_TERMIOS	0x0002
#define TTY_DRIVER_REAL_RAW		0x0004
#define TTY_DRIVER_DYNAMIC_DEV		0x0008
#define TTY_DRIVER_DEVPTS_MEM		0x0010
#define TTY_DRIVER_HARDWARE_BREAK	0x0020
#define TTY_DRIVER_DYNAMIC_ALLOC	0x0040
#define TTY_DRIVER_UNNUMBERED_NODE	0x0080

/* tty driver types */
#define TTY_DRIVER_TYPE_SYSTEM		0x0001
#define TTY_DRIVER_TYPE_CONSOLE		0x0002
#define TTY_DRIVER_TYPE_SERIAL		0x0003
#define TTY_DRIVER_TYPE_PTY		0x0004
#define TTY_DRIVER_TYPE_SCC		0x0005	/* scc driver */
#define TTY_DRIVER_TYPE_SYSCONS		0x0006

/* system subtypes (magic, used by tty_io.c) */
#define SYSTEM_TYPE_TTY			0x0001
#define SYSTEM_TYPE_CONSOLE		0x0002
#define SYSTEM_TYPE_SYSCONS		0x0003
#define SYSTEM_TYPE_SYSPTMX		0x0004

/* pty subtypes (magic, used by tty_io.c) */
#define PTY_TYPE_MASTER			0x0001
#define PTY_TYPE_SLAVE			0x0002

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1

int tty_register_driver(struct tty_driver *driver);
void tty_unregister_driver(struct tty_driver *driver);
struct device *tty_register_device(struct tty_driver *driver, unsigned index,
		struct device *dev);
struct device *tty_register_device_attr(struct tty_driver *driver,
		unsigned index, struct device *device, void *drvdata,
		const struct attribute_group **attr_grp);
void tty_unregister_device(struct tty_driver *driver, unsigned index);

#ifdef CONFIG_PROC_FS
void proc_tty_register_driver(struct tty_driver *);
void proc_tty_unregister_driver(struct tty_driver *);
#else
static inline void proc_tty_register_driver(struct tty_driver *d) {}
static inline void proc_tty_unregister_driver(struct tty_driver *d) {}
#endif

#endif /* #ifdef _LINUX_TTY_DRIVER_H */
