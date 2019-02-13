/*
 * Any part of this program may be used in documents licensed under
 * the GNU Free Documentation License, Version 1.1 or any later version
 * published by the Free Software Foundation.
 */
#ifndef _PARPORT_H_
#define _PARPORT_H_


#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/irqreturn.h>
#include <linux/semaphore.h>
#include <linux/device.h>
#include <asm/ptrace.h>
#include <uapi/linux/parport.h>

/* Define this later. */
struct parport;
struct pardevice;

struct pc_parport_state {
	unsigned int ctr;
	unsigned int ecr;
};

struct ax_parport_state {
	unsigned int ctr;
	unsigned int ecr;
	unsigned int dcsr;
};

/* used by both parport_amiga and parport_mfc3 */
struct amiga_parport_state {
       unsigned char data;     /* ciaa.prb */
       unsigned char datadir;  /* ciaa.ddrb */
       unsigned char status;   /* ciab.pra & 7 */
       unsigned char statusdir;/* ciab.ddrb & 7 */
};

struct ax88796_parport_state {
	unsigned char cpr;
};

struct ip32_parport_state {
	unsigned int dcr;
	unsigned int ecr;
};

struct parport_state {
	union {
		struct pc_parport_state pc;
		/* ARC has no state. */
		struct ax_parport_state ax;
		struct amiga_parport_state amiga;
		struct ax88796_parport_state ax88796;
		/* Atari has not state. */
		struct ip32_parport_state ip32;
		void *misc; 
	} u;
};

struct parport_operations {
	/* IBM PC-style virtual registers. */
	void (*write_data)(struct parport *, unsigned char);
	unsigned char (*read_data)(struct parport *);

	void (*write_control)(struct parport *, unsigned char);
	unsigned char (*read_control)(struct parport *);
	unsigned char (*frob_control)(struct parport *, unsigned char mask,
				      unsigned char val);

	unsigned char (*read_status)(struct parport *);

	/* IRQs. */
	void (*enable_irq)(struct parport *);
	void (*disable_irq)(struct parport *);

	/* Data direction. */
	void (*data_forward) (struct parport *);
	void (*data_reverse) (struct parport *);

	/* For core parport code. */
	void (*init_state)(struct pardevice *, struct parport_state *);
	void (*save_state)(struct parport *, struct parport_state *);
	void (*restore_state)(struct parport *, struct parport_state *);

	/* Block read/write */
	size_t (*epp_write_data) (struct parport *port, const void *buf,
				  size_t len, int flags);
	size_t (*epp_read_data) (struct parport *port, void *buf, size_t len,
				 int flags);
	size_t (*epp_write_addr) (struct parport *port, const void *buf,
				  size_t len, int flags);
	size_t (*epp_read_addr) (struct parport *port, void *buf, size_t len,
				 int flags);

	size_t (*ecp_write_data) (struct parport *port, const void *buf,
				  size_t len, int flags);
	size_t (*ecp_read_data) (struct parport *port, void *buf, size_t len,
				 int flags);
	size_t (*ecp_write_addr) (struct parport *port, const void *buf,
				  size_t len, int flags);

	size_t (*compat_write_data) (struct parport *port, const void *buf,
				     size_t len, int flags);
	size_t (*nibble_read_data) (struct parport *port, void *buf,
				    size_t len, int flags);
	size_t (*byte_read_data) (struct parport *port, void *buf,
				  size_t len, int flags);
	struct module *owner;
};

struct parport_device_info {
	parport_device_class class;
	const char *class_name;
	const char *mfr;
	const char *model;
	const char *cmdset;
	const char *description;
};

/* Each device can have two callback functions:
 *  1) a preemption function, called by the resource manager to request
 *     that the driver relinquish control of the port.  The driver should
 *     return zero if it agrees to release the port, and nonzero if it 
 *     refuses.  Do not call parport_release() - the kernel will do this
 *     implicitly.
 *
 *  2) a wake-up function, called by the resource manager to tell drivers
 *     that the port is available to be claimed.  If a driver wants to use
 *     the port, it should call parport_claim() here.
 */

/* A parallel port device */
struct pardevice {
	const char *name;
	struct parport *port;
	int daisy;
	int (*preempt)(void *);
	void (*wakeup)(void *);
	void *private;
	void (*irq_func)(void *);
	unsigned int flags;
	struct pardevice *next;
	struct pardevice *prev;
	struct device dev;
	bool devmodel;
	struct parport_state *state;     /* saved status over preemption */
	wait_queue_head_t wait_q;
	unsigned long int time;
	unsigned long int timeslice;
	volatile long int timeout;
	unsigned long waiting;		 /* long req'd for set_bit --RR */
	struct pardevice *waitprev;
	struct pardevice *waitnext;
	void * sysctl_table;
};

#define to_pardevice(n) container_of(n, struct pardevice, dev)

/* IEEE1284 information */

/* IEEE1284 phases. These are exposed to userland through ppdev IOCTL
 * PP[GS]ETPHASE, so do not change existing values. */
enum ieee1284_phase {
	IEEE1284_PH_FWD_DATA,
	IEEE1284_PH_FWD_IDLE,
	IEEE1284_PH_TERMINATE,
	IEEE1284_PH_NEGOTIATION,
	IEEE1284_PH_HBUSY_DNA,
	IEEE1284_PH_REV_IDLE,
	IEEE1284_PH_HBUSY_DAVAIL,
	IEEE1284_PH_REV_DATA,
	IEEE1284_PH_ECP_SETUP,
	IEEE1284_PH_ECP_FWD_TO_REV,
	IEEE1284_PH_ECP_REV_TO_FWD,
	IEEE1284_PH_ECP_DIR_UNKNOWN,
};
struct ieee1284_info {
	int mode;
	volatile enum ieee1284_phase phase;
	struct semaphore irq;
};

/* A parallel port */
struct parport {
	unsigned long base;	/* base address */
	unsigned long base_hi;  /* base address (hi - ECR) */
	unsigned int size;	/* IO extent */
	const char *name;
	unsigned int modes;
	int irq;		/* interrupt (or -1 for none) */
	int dma;
	int muxport;		/* which muxport (if any) this is */
	int portnum;		/* which physical parallel port (not mux) */
	struct device *dev;	/* Physical device associated with IO/DMA.
				 * This may unfortulately be null if the
				 * port has a legacy driver.
				 */
	struct device bus_dev;	/* to link with the bus */
	struct parport *physport;
				/* If this is a non-default mux
				   parport, i.e. we're a clone of a real
				   physical port, this is a pointer to that
				   port. The locking is only done in the
				   real port.  For a clone port, the
				   following structure members are
				   meaningless: devices, cad, muxsel,
				   waithead, waittail, flags, pdir,
				   dev, ieee1284, *_lock.

				   It this is a default mux parport, or
				   there is no mux involved, this points to
				   ourself. */

	struct pardevice *devices;
	struct pardevice *cad;	/* port owner */
	int daisy;		/* currently selected daisy addr */
	int muxsel;		/* currently selected mux port */

	struct pardevice *waithead;
	struct pardevice *waittail;

	struct list_head list;
	struct timer_list timer;
	unsigned int flags;

	void *sysctl_table;
	struct parport_device_info probe_info[5]; /* 0-3 + non-IEEE1284.3 */
	struct ieee1284_info ieee1284;

	struct parport_operations *ops;
	void *private_data;     /* for lowlevel driver */

	int number;		/* port index - the `n' in `parportn' */
	spinlock_t pardevice_lock;
	spinlock_t waitlist_lock;
	rwlock_t cad_lock;

	int spintime;
	atomic_t ref_count;

	unsigned long devflags;
#define PARPORT_DEVPROC_REGISTERED	0
	struct pardevice *proc_device;	/* Currently register proc device */

	struct list_head full_list;
	struct parport *slaves[3];
};

#define to_parport_dev(n) container_of(n, struct parport, bus_dev)

#define DEFAULT_SPIN_TIME 500 /* us */

struct parport_driver {
	const char *name;
	void (*attach) (struct parport *);
	void (*detach) (struct parport *);
	void (*match_port)(struct parport *);
	int (*probe)(struct pardevice *);
	struct device_driver driver;
	bool devmodel;
	struct list_head list;
};

#define to_parport_driver(n) container_of(n, struct parport_driver, driver)

int parport_bus_init(void);
void parport_bus_exit(void);

/* parport_register_port registers a new parallel port at the given
   address (if one does not already exist) and returns a pointer to it.
   This entails claiming the I/O region, IRQ and DMA.  NULL is returned
   if initialisation fails. */
struct parport *parport_register_port(unsigned long base, int irq, int dma,
				      struct parport_operations *ops);

/* Once a registered port is ready for high-level drivers to use, the
   low-level driver that registered it should announce it.  This will
   call the high-level drivers' attach() functions (after things like
   determining the IEEE 1284.3 topology of the port and collecting
   DeviceIDs). */
void parport_announce_port (struct parport *port);

/* Unregister a port. */
extern void parport_remove_port(struct parport *port);

/* Register a new high-level driver. */

int __must_check __parport_register_driver(struct parport_driver *,
					   struct module *,
					   const char *mod_name);
/*
 * parport_register_driver must be a macro so that KBUILD_MODNAME can
 * be expanded
 */
#define parport_register_driver(driver)             \
	__parport_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

/* Unregister a high-level driver. */
extern void parport_unregister_driver (struct parport_driver *);
void parport_unregister_driver(struct parport_driver *);

/* If parport_register_driver doesn't fit your needs, perhaps
 * parport_find_xxx does. */
extern struct parport *parport_find_number (int);
extern struct parport *parport_find_base (unsigned long);

/* generic irq handler, if it suits your needs */
extern irqreturn_t parport_irq_handler(int irq, void *dev_id);

/* Reference counting for ports. */
extern struct parport *parport_get_port (struct parport *);
extern void parport_put_port (struct parport *);
void parport_del_port(struct parport *);

struct pardev_cb {
	int (*preempt)(void *);
	void (*wakeup)(void *);
	void *private;
	void (*irq_func)(void *);
	unsigned int flags;
};

/* parport_register_device declares that a device is connected to a
   port, and tells the kernel all it needs to know.
   - pf is the preemption function (may be NULL for no callback)
   - kf is the wake-up function (may be NULL for no callback)
   - irq_func is the interrupt handler (may be NULL for no interrupts)
   - handle is a user pointer that gets handed to callback functions.  */
struct pardevice *parport_register_device(struct parport *port, 
			  const char *name,
			  int (*pf)(void *), void (*kf)(void *),
			  void (*irq_func)(void *), 
			  int flags, void *handle);

struct pardevice *
parport_register_dev_model(struct parport *port, const char *name,
			   const struct pardev_cb *par_dev_cb, int cnt);

/* parport_unregister unlinks a device from the chain. */
extern void parport_unregister_device(struct pardevice *dev);

/* parport_claim tries to gain ownership of the port for a particular
   driver.  This may fail (return non-zero) if another driver is busy.
   If this driver has registered an interrupt handler, it will be
   enabled.  */
extern int parport_claim(struct pardevice *dev);

/* parport_claim_or_block is the same, but sleeps if the port cannot
   be claimed.  Return value is 1 if it slept, 0 normally and -errno
   on error.  */
extern int parport_claim_or_block(struct pardevice *dev);

/* parport_release reverses a previous parport_claim.  This can never
   fail, though the effects are undefined (except that they are bad)
   if you didn't previously own the port.  Once you have released the
   port you should make sure that neither your code nor the hardware
   on the port tries to initiate any communication without first
   re-claiming the port.  If you mess with the port state (enabling
   ECP for example) you should clean up before releasing the port. */

extern void parport_release(struct pardevice *dev);

/**
 * parport_yield - relinquish a parallel port temporarily
 * @dev: a device on the parallel port
 *
 * This function relinquishes the port if it would be helpful to other
 * drivers to do so.  Afterwards it tries to reclaim the port using
 * parport_claim(), and the return value is the same as for
 * parport_claim().  If it fails, the port is left unclaimed and it is
 * the driver's responsibility to reclaim the port.
 *
 * The parport_yield() and parport_yield_blocking() functions are for
 * marking points in the driver at which other drivers may claim the
 * port and use their devices.  Yielding the port is similar to
 * releasing it and reclaiming it, but is more efficient because no
 * action is taken if there are no other devices needing the port.  In
 * fact, nothing is done even if there are other devices waiting but
 * the current device is still within its "timeslice".  The default
 * timeslice is half a second, but it can be adjusted via the /proc
 * interface.
 **/
static __inline__ int parport_yield(struct pardevice *dev)
{
	unsigned long int timeslip = (jiffies - dev->time);
	if ((dev->port->waithead == NULL) || (timeslip < dev->timeslice))
		return 0;
	parport_release(dev);
	return parport_claim(dev);
}

/**
 * parport_yield_blocking - relinquish a parallel port temporarily
 * @dev: a device on the parallel port
 *
 * This function relinquishes the port if it would be helpful to other
 * drivers to do so.  Afterwards it tries to reclaim the port using
 * parport_claim_or_block(), and the return value is the same as for
 * parport_claim_or_block().
 **/
static __inline__ int parport_yield_blocking(struct pardevice *dev)
{
	unsigned long int timeslip = (jiffies - dev->time);
	if ((dev->port->waithead == NULL) || (timeslip < dev->timeslice))
		return 0;
	parport_release(dev);
	return parport_claim_or_block(dev);
}

/* Flags used to identify what a device does. */
#define PARPORT_DEV_TRAN		0	/* WARNING !! DEPRECATED !! */
#define PARPORT_DEV_LURK		(1<<0)	/* WARNING !! DEPRECATED !! */
#define PARPORT_DEV_EXCL		(1<<1)	/* Need exclusive access. */

#define PARPORT_FLAG_EXCL		(1<<1)	/* EXCL driver registered. */

/* IEEE1284 functions */
extern void parport_ieee1284_interrupt (void *);
extern int parport_negotiate (struct parport *, int mode);
extern ssize_t parport_write (struct parport *, const void *buf, size_t len);
extern ssize_t parport_read (struct parport *, void *buf, size_t len);

#define PARPORT_INACTIVITY_O_NONBLOCK 1
extern long parport_set_timeout (struct pardevice *, long inactivity);

extern int parport_wait_event (struct parport *, long timeout);
extern int parport_wait_peripheral (struct parport *port,
				    unsigned char mask,
				    unsigned char val);
extern int parport_poll_peripheral (struct parport *port,
				    unsigned char mask,
				    unsigned char val,
				    int usec);

/* For architectural drivers */
extern size_t parport_ieee1284_write_compat (struct parport *,
					     const void *, size_t, int);
extern size_t parport_ieee1284_read_nibble (struct parport *,
					    void *, size_t, int);
extern size_t parport_ieee1284_read_byte (struct parport *,
					  void *, size_t, int);
extern size_t parport_ieee1284_ecp_read_data (struct parport *,
					      void *, size_t, int);
extern size_t parport_ieee1284_ecp_write_data (struct parport *,
					       const void *, size_t, int);
extern size_t parport_ieee1284_ecp_write_addr (struct parport *,
					       const void *, size_t, int);
extern size_t parport_ieee1284_epp_write_data (struct parport *,
					       const void *, size_t, int);
extern size_t parport_ieee1284_epp_read_data (struct parport *,
					      void *, size_t, int);
extern size_t parport_ieee1284_epp_write_addr (struct parport *,
					       const void *, size_t, int);
extern size_t parport_ieee1284_epp_read_addr (struct parport *,
					      void *, size_t, int);

/* IEEE1284.3 functions */
#define daisy_dev_name "Device ID probe"
extern int parport_daisy_init (struct parport *port);
extern void parport_daisy_fini (struct parport *port);
extern struct pardevice *parport_open (int devnum, const char *name);
extern void parport_close (struct pardevice *dev);
extern ssize_t parport_device_id (int devnum, char *buffer, size_t len);
extern void parport_daisy_deselect_all (struct parport *port);
extern int parport_daisy_select (struct parport *port, int daisy, int mode);

#ifdef CONFIG_PARPORT_1284
extern int daisy_drv_init(void);
extern void daisy_drv_exit(void);
#else
static inline int daisy_drv_init(void)
{
	return 0;
}

static inline void daisy_drv_exit(void) {}
#endif

/* Lowlevel drivers _can_ call this support function to handle irqs.  */
static inline void parport_generic_irq(struct parport *port)
{
	parport_ieee1284_interrupt (port);
	read_lock(&port->cad_lock);
	if (port->cad && port->cad->irq_func)
		port->cad->irq_func(port->cad->private);
	read_unlock(&port->cad_lock);
}

/* Prototypes from parport_procfs */
extern int parport_proc_register(struct parport *pp);
extern int parport_proc_unregister(struct parport *pp);
extern int parport_device_proc_register(struct pardevice *device);
extern int parport_device_proc_unregister(struct pardevice *device);

/* If PC hardware is the only type supported, we can optimise a bit.  */
#if !defined(CONFIG_PARPORT_NOT_PC)

#include <linux/parport_pc.h>
#define parport_write_data(p,x)            parport_pc_write_data(p,x)
#define parport_read_data(p)               parport_pc_read_data(p)
#define parport_write_control(p,x)         parport_pc_write_control(p,x)
#define parport_read_control(p)            parport_pc_read_control(p)
#define parport_frob_control(p,m,v)        parport_pc_frob_control(p,m,v)
#define parport_read_status(p)             parport_pc_read_status(p)
#define parport_enable_irq(p)              parport_pc_enable_irq(p)
#define parport_disable_irq(p)             parport_pc_disable_irq(p)
#define parport_data_forward(p)            parport_pc_data_forward(p)
#define parport_data_reverse(p)            parport_pc_data_reverse(p)

#else  /*  !CONFIG_PARPORT_NOT_PC  */

/* Generic operations vector through the dispatch table. */
#define parport_write_data(p,x)            (p)->ops->write_data(p,x)
#define parport_read_data(p)               (p)->ops->read_data(p)
#define parport_write_control(p,x)         (p)->ops->write_control(p,x)
#define parport_read_control(p)            (p)->ops->read_control(p)
#define parport_frob_control(p,m,v)        (p)->ops->frob_control(p,m,v)
#define parport_read_status(p)             (p)->ops->read_status(p)
#define parport_enable_irq(p)              (p)->ops->enable_irq(p)
#define parport_disable_irq(p)             (p)->ops->disable_irq(p)
#define parport_data_forward(p)            (p)->ops->data_forward(p)
#define parport_data_reverse(p)            (p)->ops->data_reverse(p)

#endif /*  !CONFIG_PARPORT_NOT_PC  */

extern unsigned long parport_default_timeslice;
extern int parport_default_spintime;

#endif /* _PARPORT_H_ */
