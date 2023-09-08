/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>
#include <linux/wait.h>

struct attribute_group;
struct tty_driver;
struct tty_port;
struct tty_struct;

/**
 * struct tty_port_operations -- operations on tty_port
 * @carrier_raised: return true if the carrier is raised on @port
 * @dtr_rts: raise the DTR line if @active is true, otherwise lower DTR
 * @shutdown: called when the last close completes or a hangup finishes IFF the
 *	port was initialized. Do not use to free resources. Turn off the device
 *	only. Called under the port mutex to serialize against @activate and
 *	@shutdown.
 * @activate: called under the port mutex from tty_port_open(), serialized using
 *	the port mutex. Supposed to turn on the device.
 *
 *	FIXME: long term getting the tty argument *out* of this would be good
 *	for consoles.
 *
 * @destruct: called on the final put of a port. Free resources, possibly incl.
 *	the port itself.
 */
struct tty_port_operations {
	bool (*carrier_raised)(struct tty_port *port);
	void (*dtr_rts)(struct tty_port *port, bool active);
	void (*shutdown)(struct tty_port *port);
	int (*activate)(struct tty_port *port, struct tty_struct *tty);
	void (*destruct)(struct tty_port *port);
};

struct tty_port_client_operations {
	int (*receive_buf)(struct tty_port *port, const unsigned char *, const unsigned char *, size_t);
	void (*lookahead_buf)(struct tty_port *port, const unsigned char *cp,
			      const unsigned char *fp, unsigned int count);
	void (*write_wakeup)(struct tty_port *port);
};

extern const struct tty_port_client_operations tty_port_default_client_ops;

/**
 * struct tty_port -- port level information
 *
 * @buf: buffer for this port, locked internally
 * @tty: back pointer to &struct tty_struct, valid only if the tty is open. Use
 *	 tty_port_tty_get() to obtain it (and tty_kref_put() to release).
 * @itty: internal back pointer to &struct tty_struct. Avoid this. It should be
 *	  eliminated in the long term.
 * @ops: tty port operations (like activate, shutdown), see &struct
 *	 tty_port_operations
 * @client_ops: tty port client operations (like receive_buf, write_wakeup).
 *		By default, tty_port_default_client_ops is used.
 * @lock: lock protecting @tty
 * @blocked_open: # of procs waiting for open in tty_port_block_til_ready()
 * @count: usage count
 * @open_wait: open waiters queue (waiting e.g. for a carrier)
 * @delta_msr_wait: modem status change queue (waiting for MSR changes)
 * @flags: user TTY flags (%ASYNC_)
 * @iflags: internal flags (%TTY_PORT_)
 * @console: when set, the port is a console
 * @mutex: locking, for open, shutdown and other port operations
 * @buf_mutex: @xmit_buf alloc lock
 * @xmit_buf: optional xmit buffer used by some drivers
 * @xmit_fifo: optional xmit buffer used by some drivers
 * @close_delay: delay in jiffies to wait when closing the port
 * @closing_wait: delay in jiffies for output to be sent before closing
 * @drain_delay: set to zero if no pure time based drain is needed else set to
 *		 size of fifo
 * @kref: references counter. Reaching zero calls @ops->destruct() if non-%NULL
 *	  or frees the port otherwise.
 * @client_data: pointer to private data, for @client_ops
 *
 * Each device keeps its own port level information. &struct tty_port was
 * introduced as a common structure for such information. As every TTY device
 * shall have a backing tty_port structure, every driver can use these members.
 *
 * The tty port has a different lifetime to the tty so must be kept apart.
 * In addition be careful as tty -> port mappings are valid for the life
 * of the tty object but in many cases port -> tty mappings are valid only
 * until a hangup so don't use the wrong path.
 *
 * Tty port shall be initialized by tty_port_init() and shut down either by
 * tty_port_destroy() (refcounting not used), or tty_port_put() (refcounting).
 *
 * There is a lot of helpers around &struct tty_port too. To name the most
 * significant ones: tty_port_open(), tty_port_close() (or
 * tty_port_close_start() and tty_port_close_end() separately if need be), and
 * tty_port_hangup(). These call @ops->activate() and @ops->shutdown() as
 * needed.
 */
struct tty_port {
	struct tty_bufhead	buf;
	struct tty_struct	*tty;
	struct tty_struct	*itty;
	const struct tty_port_operations *ops;
	const struct tty_port_client_operations *client_ops;
	spinlock_t		lock;
	int			blocked_open;
	int			count;
	wait_queue_head_t	open_wait;
	wait_queue_head_t	delta_msr_wait;
	unsigned long		flags;
	unsigned long		iflags;
	unsigned char		console:1;
	struct mutex		mutex;
	struct mutex		buf_mutex;
	unsigned char		*xmit_buf;
	DECLARE_KFIFO_PTR(xmit_fifo, unsigned char);
	unsigned int		close_delay;
	unsigned int		closing_wait;
	int			drain_delay;
	struct kref		kref;
	void			*client_data;
};

/* tty_port::iflags bits -- use atomic bit ops */
#define TTY_PORT_INITIALIZED	0	/* device is initialized */
#define TTY_PORT_SUSPENDED	1	/* device is suspended */
#define TTY_PORT_ACTIVE		2	/* device is open */

/*
 * uart drivers: use the uart_port::status field and the UPSTAT_* defines
 * for s/w-based flow control steering and carrier detection status
 */
#define TTY_PORT_CTS_FLOW	3	/* h/w flow control enabled */
#define TTY_PORT_CHECK_CD	4	/* carrier detect enabled */
#define TTY_PORT_KOPENED	5	/* device exclusively opened by
					   kernel */

void tty_port_init(struct tty_port *port);
void tty_port_link_device(struct tty_port *port, struct tty_driver *driver,
		unsigned index);
struct device *tty_port_register_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device);
struct device *tty_port_register_device_attr(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device, void *drvdata,
		const struct attribute_group **attr_grp);
struct device *tty_port_register_device_serdev(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device);
struct device *tty_port_register_device_attr_serdev(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device, void *drvdata,
		const struct attribute_group **attr_grp);
void tty_port_unregister_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index);
int tty_port_alloc_xmit_buf(struct tty_port *port);
void tty_port_free_xmit_buf(struct tty_port *port);
void tty_port_destroy(struct tty_port *port);
void tty_port_put(struct tty_port *port);

static inline struct tty_port *tty_port_get(struct tty_port *port)
{
	if (port && kref_get_unless_zero(&port->kref))
		return port;
	return NULL;
}

/* If the cts flow control is enabled, return true. */
static inline bool tty_port_cts_enabled(const struct tty_port *port)
{
	return test_bit(TTY_PORT_CTS_FLOW, &port->iflags);
}

static inline void tty_port_set_cts_flow(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_CTS_FLOW, &port->iflags, val);
}

static inline bool tty_port_active(const struct tty_port *port)
{
	return test_bit(TTY_PORT_ACTIVE, &port->iflags);
}

static inline void tty_port_set_active(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_ACTIVE, &port->iflags, val);
}

static inline bool tty_port_check_carrier(const struct tty_port *port)
{
	return test_bit(TTY_PORT_CHECK_CD, &port->iflags);
}

static inline void tty_port_set_check_carrier(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_CHECK_CD, &port->iflags, val);
}

static inline bool tty_port_suspended(const struct tty_port *port)
{
	return test_bit(TTY_PORT_SUSPENDED, &port->iflags);
}

static inline void tty_port_set_suspended(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_SUSPENDED, &port->iflags, val);
}

static inline bool tty_port_initialized(const struct tty_port *port)
{
	return test_bit(TTY_PORT_INITIALIZED, &port->iflags);
}

static inline void tty_port_set_initialized(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_INITIALIZED, &port->iflags, val);
}

static inline bool tty_port_kopened(const struct tty_port *port)
{
	return test_bit(TTY_PORT_KOPENED, &port->iflags);
}

static inline void tty_port_set_kopened(struct tty_port *port, bool val)
{
	assign_bit(TTY_PORT_KOPENED, &port->iflags, val);
}

struct tty_struct *tty_port_tty_get(struct tty_port *port);
void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty);
bool tty_port_carrier_raised(struct tty_port *port);
void tty_port_raise_dtr_rts(struct tty_port *port);
void tty_port_lower_dtr_rts(struct tty_port *port);
void tty_port_hangup(struct tty_port *port);
void tty_port_tty_hangup(struct tty_port *port, bool check_clocal);
void tty_port_tty_wakeup(struct tty_port *port);
int tty_port_block_til_ready(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
int tty_port_close_start(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
void tty_port_close_end(struct tty_port *port, struct tty_struct *tty);
void tty_port_close(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);
int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty);
int tty_port_open(struct tty_port *port, struct tty_struct *tty,
		struct file *filp);

static inline int tty_port_users(struct tty_port *port)
{
	return port->count + port->blocked_open;
}

#endif
