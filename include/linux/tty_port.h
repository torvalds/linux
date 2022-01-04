/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TTY_PORT_H
#define _LINUX_TTY_PORT_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/tty_buffer.h>
#include <linux/wait.h>

/*
 * Port level information. Each device keeps its own port level information
 * so provide a common structure for those ports wanting to use common support
 * routines.
 *
 * The tty port has a different lifetime to the tty so must be kept apart.
 * In addition be careful as tty -> port mappings are valid for the life
 * of the tty object but in many cases port -> tty mappings are valid only
 * until a hangup so don't use the wrong path.
 */

struct attribute_group;
struct tty_driver;
struct tty_port;
struct tty_struct;

struct tty_port_operations {
	/* Return 1 if the carrier is raised */
	int (*carrier_raised)(struct tty_port *port);
	/* Control the DTR line */
	void (*dtr_rts)(struct tty_port *port, int raise);
	/* Called when the last close completes or a hangup finishes
	   IFF the port was initialized. Do not use to free resources. Called
	   under the port mutex to serialize against activate/shutdowns */
	void (*shutdown)(struct tty_port *port);
	/* Called under the port mutex from tty_port_open, serialized using
	   the port mutex */
        /* FIXME: long term getting the tty argument *out* of this would be
           good for consoles */
	int (*activate)(struct tty_port *port, struct tty_struct *tty);
	/* Called on the final put of a port */
	void (*destruct)(struct tty_port *port);
};

struct tty_port_client_operations {
	int (*receive_buf)(struct tty_port *port, const unsigned char *, const unsigned char *, size_t);
	void (*write_wakeup)(struct tty_port *port);
};

extern const struct tty_port_client_operations tty_port_default_client_ops;

struct tty_port {
	struct tty_bufhead	buf;		/* Locked internally */
	struct tty_struct	*tty;		/* Back pointer */
	struct tty_struct	*itty;		/* internal back ptr */
	const struct tty_port_operations *ops;	/* Port operations */
	const struct tty_port_client_operations *client_ops; /* Port client operations */
	spinlock_t		lock;		/* Lock protecting tty field */
	int			blocked_open;	/* Waiting to open */
	int			count;		/* Usage count */
	wait_queue_head_t	open_wait;	/* Open waiters */
	wait_queue_head_t	delta_msr_wait;	/* Modem status change */
	unsigned long		flags;		/* User TTY flags ASYNC_ */
	unsigned long		iflags;		/* Internal flags TTY_PORT_ */
	unsigned char		console:1;	/* port is a console */
	struct mutex		mutex;		/* Locking */
	struct mutex		buf_mutex;	/* Buffer alloc lock */
	unsigned char		*xmit_buf;	/* Optional buffer */
	unsigned int		close_delay;	/* Close port delay */
	unsigned int		closing_wait;	/* Delay for output */
	int			drain_delay;	/* Set to zero if no pure time
						   based drain is needed else
						   set to size of fifo */
	struct kref		kref;		/* Ref counter */
	void 			*client_data;
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
int tty_port_carrier_raised(struct tty_port *port);
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
