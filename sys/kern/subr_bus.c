/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997,1998,2003 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/eventhandler.h>
#include <sys/filio.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/condvar.h>
#include <sys/queue.h>
#include <machine/bus.h>
#include <sys/random.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/selinfo.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/cpuset.h>

#include <net/vnet.h>

#include <machine/cpu.h>
#include <machine/stdarg.h>

#include <vm/uma.h>
#include <vm/vm.h>

#include <ddb/ddb.h>

SYSCTL_NODE(_hw, OID_AUTO, bus, CTLFLAG_RW, NULL, NULL);
SYSCTL_ROOT_NODE(OID_AUTO, dev, CTLFLAG_RW, NULL, NULL);

/*
 * Used to attach drivers to devclasses.
 */
typedef struct driverlink *driverlink_t;
struct driverlink {
	kobj_class_t	driver;
	TAILQ_ENTRY(driverlink) link;	/* list of drivers in devclass */
	int		pass;
	int		flags;
#define DL_DEFERRED_PROBE	1	/* Probe deferred on this */
	TAILQ_ENTRY(driverlink) passlink;
};

/*
 * Forward declarations
 */
typedef TAILQ_HEAD(devclass_list, devclass) devclass_list_t;
typedef TAILQ_HEAD(driver_list, driverlink) driver_list_t;
typedef TAILQ_HEAD(device_list, device) device_list_t;

struct devclass {
	TAILQ_ENTRY(devclass) link;
	devclass_t	parent;		/* parent in devclass hierarchy */
	driver_list_t	drivers;     /* bus devclasses store drivers for bus */
	char		*name;
	device_t	*devices;	/* array of devices indexed by unit */
	int		maxunit;	/* size of devices array */
	int		flags;
#define DC_HAS_CHILDREN		1

	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
};

/**
 * @brief Implementation of device.
 */
struct device {
	/*
	 * A device is a kernel object. The first field must be the
	 * current ops table for the object.
	 */
	KOBJ_FIELDS;

	/*
	 * Device hierarchy.
	 */
	TAILQ_ENTRY(device)	link;	/**< list of devices in parent */
	TAILQ_ENTRY(device)	devlink; /**< global device list membership */
	device_t	parent;		/**< parent of this device  */
	device_list_t	children;	/**< list of child devices */

	/*
	 * Details of this device.
	 */
	driver_t	*driver;	/**< current driver */
	devclass_t	devclass;	/**< current device class */
	int		unit;		/**< current unit number */
	char*		nameunit;	/**< name+unit e.g. foodev0 */
	char*		desc;		/**< driver specific description */
	int		busy;		/**< count of calls to device_busy() */
	device_state_t	state;		/**< current device state  */
	uint32_t	devflags;	/**< api level flags for device_get_flags() */
	u_int		flags;		/**< internal device flags  */
	u_int	order;			/**< order from device_add_child_ordered() */
	void	*ivars;			/**< instance variables  */
	void	*softc;			/**< current driver's variables  */

	struct sysctl_ctx_list sysctl_ctx; /**< state for sysctl variables  */
	struct sysctl_oid *sysctl_tree;	/**< state for sysctl variables */
};

static MALLOC_DEFINE(M_BUS, "bus", "Bus data structures");
static MALLOC_DEFINE(M_BUS_SC, "bus-sc", "Bus data structures, softc");

EVENTHANDLER_LIST_DEFINE(device_attach);
EVENTHANDLER_LIST_DEFINE(device_detach);
EVENTHANDLER_LIST_DEFINE(dev_lookup);

static void devctl2_init(void);
static bool device_frozen;

#define DRIVERNAME(d)	((d)? d->name : "no driver")
#define DEVCLANAME(d)	((d)? d->name : "no devclass")

#ifdef BUS_DEBUG

static int bus_debug = 1;
SYSCTL_INT(_debug, OID_AUTO, bus_debug, CTLFLAG_RWTUN, &bus_debug, 0,
    "Bus debug level");

#define PDEBUG(a)	if (bus_debug) {printf("%s:%d: ", __func__, __LINE__), printf a; printf("\n");}
#define DEVICENAME(d)	((d)? device_get_name(d): "no device")

/**
 * Produce the indenting, indent*2 spaces plus a '.' ahead of that to
 * prevent syslog from deleting initial spaces
 */
#define indentprintf(p)	do { int iJ; printf("."); for (iJ=0; iJ<indent; iJ++) printf("  "); printf p ; } while (0)

static void print_device_short(device_t dev, int indent);
static void print_device(device_t dev, int indent);
void print_device_tree_short(device_t dev, int indent);
void print_device_tree(device_t dev, int indent);
static void print_driver_short(driver_t *driver, int indent);
static void print_driver(driver_t *driver, int indent);
static void print_driver_list(driver_list_t drivers, int indent);
static void print_devclass_short(devclass_t dc, int indent);
static void print_devclass(devclass_t dc, int indent);
void print_devclass_list_short(void);
void print_devclass_list(void);

#else
/* Make the compiler ignore the function calls */
#define PDEBUG(a)			/* nop */
#define DEVICENAME(d)			/* nop */

#define print_device_short(d,i)		/* nop */
#define print_device(d,i)		/* nop */
#define print_device_tree_short(d,i)	/* nop */
#define print_device_tree(d,i)		/* nop */
#define print_driver_short(d,i)		/* nop */
#define print_driver(d,i)		/* nop */
#define print_driver_list(d,i)		/* nop */
#define print_devclass_short(d,i)	/* nop */
#define print_devclass(d,i)		/* nop */
#define print_devclass_list_short()	/* nop */
#define print_devclass_list()		/* nop */
#endif

/*
 * dev sysctl tree
 */

enum {
	DEVCLASS_SYSCTL_PARENT,
};

static int
devclass_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	devclass_t dc = (devclass_t)arg1;
	const char *value;

	switch (arg2) {
	case DEVCLASS_SYSCTL_PARENT:
		value = dc->parent ? dc->parent->name : "";
		break;
	default:
		return (EINVAL);
	}
	return (SYSCTL_OUT_STR(req, value));
}

static void
devclass_sysctl_init(devclass_t dc)
{

	if (dc->sysctl_tree != NULL)
		return;
	sysctl_ctx_init(&dc->sysctl_ctx);
	dc->sysctl_tree = SYSCTL_ADD_NODE(&dc->sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_dev), OID_AUTO, dc->name,
	    CTLFLAG_RD, NULL, "");
	SYSCTL_ADD_PROC(&dc->sysctl_ctx, SYSCTL_CHILDREN(dc->sysctl_tree),
	    OID_AUTO, "%parent", CTLTYPE_STRING | CTLFLAG_RD,
	    dc, DEVCLASS_SYSCTL_PARENT, devclass_sysctl_handler, "A",
	    "parent class");
}

enum {
	DEVICE_SYSCTL_DESC,
	DEVICE_SYSCTL_DRIVER,
	DEVICE_SYSCTL_LOCATION,
	DEVICE_SYSCTL_PNPINFO,
	DEVICE_SYSCTL_PARENT,
};

static int
device_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	device_t dev = (device_t)arg1;
	const char *value;
	char *buf;
	int error;

	buf = NULL;
	switch (arg2) {
	case DEVICE_SYSCTL_DESC:
		value = dev->desc ? dev->desc : "";
		break;
	case DEVICE_SYSCTL_DRIVER:
		value = dev->driver ? dev->driver->name : "";
		break;
	case DEVICE_SYSCTL_LOCATION:
		value = buf = malloc(1024, M_BUS, M_WAITOK | M_ZERO);
		bus_child_location_str(dev, buf, 1024);
		break;
	case DEVICE_SYSCTL_PNPINFO:
		value = buf = malloc(1024, M_BUS, M_WAITOK | M_ZERO);
		bus_child_pnpinfo_str(dev, buf, 1024);
		break;
	case DEVICE_SYSCTL_PARENT:
		value = dev->parent ? dev->parent->nameunit : "";
		break;
	default:
		return (EINVAL);
	}
	error = SYSCTL_OUT_STR(req, value);
	if (buf != NULL)
		free(buf, M_BUS);
	return (error);
}

static void
device_sysctl_init(device_t dev)
{
	devclass_t dc = dev->devclass;
	int domain;

	if (dev->sysctl_tree != NULL)
		return;
	devclass_sysctl_init(dc);
	sysctl_ctx_init(&dev->sysctl_ctx);
	dev->sysctl_tree = SYSCTL_ADD_NODE_WITH_LABEL(&dev->sysctl_ctx,
	    SYSCTL_CHILDREN(dc->sysctl_tree), OID_AUTO,
	    dev->nameunit + strlen(dc->name),
	    CTLFLAG_RD, NULL, "", "device_index");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%desc", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_DESC, device_sysctl_handler, "A",
	    "device description");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%driver", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_DRIVER, device_sysctl_handler, "A",
	    "device driver name");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%location", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_LOCATION, device_sysctl_handler, "A",
	    "device location relative to parent");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%pnpinfo", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_PNPINFO, device_sysctl_handler, "A",
	    "device identification");
	SYSCTL_ADD_PROC(&dev->sysctl_ctx, SYSCTL_CHILDREN(dev->sysctl_tree),
	    OID_AUTO, "%parent", CTLTYPE_STRING | CTLFLAG_RD,
	    dev, DEVICE_SYSCTL_PARENT, device_sysctl_handler, "A",
	    "parent device");
	if (bus_get_domain(dev, &domain) == 0)
		SYSCTL_ADD_INT(&dev->sysctl_ctx,
		    SYSCTL_CHILDREN(dev->sysctl_tree), OID_AUTO, "%domain",
		    CTLFLAG_RD, NULL, domain, "NUMA domain");
}

static void
device_sysctl_update(device_t dev)
{
	devclass_t dc = dev->devclass;

	if (dev->sysctl_tree == NULL)
		return;
	sysctl_rename_oid(dev->sysctl_tree, dev->nameunit + strlen(dc->name));
}

static void
device_sysctl_fini(device_t dev)
{
	if (dev->sysctl_tree == NULL)
		return;
	sysctl_ctx_free(&dev->sysctl_ctx);
	dev->sysctl_tree = NULL;
}

/*
 * /dev/devctl implementation
 */

/*
 * This design allows only one reader for /dev/devctl.  This is not desirable
 * in the long run, but will get a lot of hair out of this implementation.
 * Maybe we should make this device a clonable device.
 *
 * Also note: we specifically do not attach a device to the device_t tree
 * to avoid potential chicken and egg problems.  One could argue that all
 * of this belongs to the root node.  One could also further argue that the
 * sysctl interface that we have not might more properly be an ioctl
 * interface, but at this stage of the game, I'm not inclined to rock that
 * boat.
 *
 * I'm also not sure that the SIGIO support is done correctly or not, as
 * I copied it from a driver that had SIGIO support that likely hasn't been
 * tested since 3.4 or 2.2.8!
 */

/* Deprecated way to adjust queue length */
static int sysctl_devctl_disable(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_hw_bus, OID_AUTO, devctl_disable, CTLTYPE_INT | CTLFLAG_RWTUN |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_devctl_disable, "I",
    "devctl disable -- deprecated");

#define DEVCTL_DEFAULT_QUEUE_LEN 1000
static int sysctl_devctl_queue(SYSCTL_HANDLER_ARGS);
static int devctl_queue_length = DEVCTL_DEFAULT_QUEUE_LEN;
SYSCTL_PROC(_hw_bus, OID_AUTO, devctl_queue, CTLTYPE_INT | CTLFLAG_RWTUN |
    CTLFLAG_MPSAFE, NULL, 0, sysctl_devctl_queue, "I", "devctl queue length");

static d_open_t		devopen;
static d_close_t	devclose;
static d_read_t		devread;
static d_ioctl_t	devioctl;
static d_poll_t		devpoll;
static d_kqfilter_t	devkqfilter;

static struct cdevsw dev_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	devopen,
	.d_close =	devclose,
	.d_read =	devread,
	.d_ioctl =	devioctl,
	.d_poll =	devpoll,
	.d_kqfilter =	devkqfilter,
	.d_name =	"devctl",
};

struct dev_event_info
{
	char *dei_data;
	TAILQ_ENTRY(dev_event_info) dei_link;
};

TAILQ_HEAD(devq, dev_event_info);

static struct dev_softc
{
	int	inuse;
	int	nonblock;
	int	queued;
	int	async;
	struct mtx mtx;
	struct cv cv;
	struct selinfo sel;
	struct devq devq;
	struct sigio *sigio;
} devsoftc;

static void	filt_devctl_detach(struct knote *kn);
static int	filt_devctl_read(struct knote *kn, long hint);

struct filterops devctl_rfiltops = {
	.f_isfd = 1,
	.f_detach = filt_devctl_detach,
	.f_event = filt_devctl_read,
};

static struct cdev *devctl_dev;

static void
devinit(void)
{
	devctl_dev = make_dev_credf(MAKEDEV_ETERNAL, &dev_cdevsw, 0, NULL,
	    UID_ROOT, GID_WHEEL, 0600, "devctl");
	mtx_init(&devsoftc.mtx, "dev mtx", "devd", MTX_DEF);
	cv_init(&devsoftc.cv, "dev cv");
	TAILQ_INIT(&devsoftc.devq);
	knlist_init_mtx(&devsoftc.sel.si_note, &devsoftc.mtx);
	devctl2_init();
}

static int
devopen(struct cdev *dev, int oflags, int devtype, struct thread *td)
{

	mtx_lock(&devsoftc.mtx);
	if (devsoftc.inuse) {
		mtx_unlock(&devsoftc.mtx);
		return (EBUSY);
	}
	/* move to init */
	devsoftc.inuse = 1;
	mtx_unlock(&devsoftc.mtx);
	return (0);
}

static int
devclose(struct cdev *dev, int fflag, int devtype, struct thread *td)
{

	mtx_lock(&devsoftc.mtx);
	devsoftc.inuse = 0;
	devsoftc.nonblock = 0;
	devsoftc.async = 0;
	cv_broadcast(&devsoftc.cv);
	funsetown(&devsoftc.sigio);
	mtx_unlock(&devsoftc.mtx);
	return (0);
}

/*
 * The read channel for this device is used to report changes to
 * userland in realtime.  We are required to free the data as well as
 * the n1 object because we allocate them separately.  Also note that
 * we return one record at a time.  If you try to read this device a
 * character at a time, you will lose the rest of the data.  Listening
 * programs are expected to cope.
 */
static int
devread(struct cdev *dev, struct uio *uio, int ioflag)
{
	struct dev_event_info *n1;
	int rv;

	mtx_lock(&devsoftc.mtx);
	while (TAILQ_EMPTY(&devsoftc.devq)) {
		if (devsoftc.nonblock) {
			mtx_unlock(&devsoftc.mtx);
			return (EAGAIN);
		}
		rv = cv_wait_sig(&devsoftc.cv, &devsoftc.mtx);
		if (rv) {
			/*
			 * Need to translate ERESTART to EINTR here? -- jake
			 */
			mtx_unlock(&devsoftc.mtx);
			return (rv);
		}
	}
	n1 = TAILQ_FIRST(&devsoftc.devq);
	TAILQ_REMOVE(&devsoftc.devq, n1, dei_link);
	devsoftc.queued--;
	mtx_unlock(&devsoftc.mtx);
	rv = uiomove(n1->dei_data, strlen(n1->dei_data), uio);
	free(n1->dei_data, M_BUS);
	free(n1, M_BUS);
	return (rv);
}

static	int
devioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	switch (cmd) {

	case FIONBIO:
		if (*(int*)data)
			devsoftc.nonblock = 1;
		else
			devsoftc.nonblock = 0;
		return (0);
	case FIOASYNC:
		if (*(int*)data)
			devsoftc.async = 1;
		else
			devsoftc.async = 0;
		return (0);
	case FIOSETOWN:
		return fsetown(*(int *)data, &devsoftc.sigio);
	case FIOGETOWN:
		*(int *)data = fgetown(&devsoftc.sigio);
		return (0);

		/* (un)Support for other fcntl() calls. */
	case FIOCLEX:
	case FIONCLEX:
	case FIONREAD:
	default:
		break;
	}
	return (ENOTTY);
}

static	int
devpoll(struct cdev *dev, int events, struct thread *td)
{
	int	revents = 0;

	mtx_lock(&devsoftc.mtx);
	if (events & (POLLIN | POLLRDNORM)) {
		if (!TAILQ_EMPTY(&devsoftc.devq))
			revents = events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &devsoftc.sel);
	}
	mtx_unlock(&devsoftc.mtx);

	return (revents);
}

static int
devkqfilter(struct cdev *dev, struct knote *kn)
{
	int error;

	if (kn->kn_filter == EVFILT_READ) {
		kn->kn_fop = &devctl_rfiltops;
		knlist_add(&devsoftc.sel.si_note, kn, 0);
		error = 0;
	} else
		error = EINVAL;
	return (error);
}

static void
filt_devctl_detach(struct knote *kn)
{

	knlist_remove(&devsoftc.sel.si_note, kn, 0);
}

static int
filt_devctl_read(struct knote *kn, long hint)
{
	kn->kn_data = devsoftc.queued;
	return (kn->kn_data != 0);
}

/**
 * @brief Return whether the userland process is running
 */
boolean_t
devctl_process_running(void)
{
	return (devsoftc.inuse == 1);
}

/**
 * @brief Queue data to be read from the devctl device
 *
 * Generic interface to queue data to the devctl device.  It is
 * assumed that @p data is properly formatted.  It is further assumed
 * that @p data is allocated using the M_BUS malloc type.
 */
void
devctl_queue_data_f(char *data, int flags)
{
	struct dev_event_info *n1 = NULL, *n2 = NULL;

	if (strlen(data) == 0)
		goto out;
	if (devctl_queue_length == 0)
		goto out;
	n1 = malloc(sizeof(*n1), M_BUS, flags);
	if (n1 == NULL)
		goto out;
	n1->dei_data = data;
	mtx_lock(&devsoftc.mtx);
	if (devctl_queue_length == 0) {
		mtx_unlock(&devsoftc.mtx);
		free(n1->dei_data, M_BUS);
		free(n1, M_BUS);
		return;
	}
	/* Leave at least one spot in the queue... */
	while (devsoftc.queued > devctl_queue_length - 1) {
		n2 = TAILQ_FIRST(&devsoftc.devq);
		TAILQ_REMOVE(&devsoftc.devq, n2, dei_link);
		free(n2->dei_data, M_BUS);
		free(n2, M_BUS);
		devsoftc.queued--;
	}
	TAILQ_INSERT_TAIL(&devsoftc.devq, n1, dei_link);
	devsoftc.queued++;
	cv_broadcast(&devsoftc.cv);
	KNOTE_LOCKED(&devsoftc.sel.si_note, 0);
	mtx_unlock(&devsoftc.mtx);
	selwakeup(&devsoftc.sel);
	if (devsoftc.async && devsoftc.sigio != NULL)
		pgsigio(&devsoftc.sigio, SIGIO, 0);
	return;
out:
	/*
	 * We have to free data on all error paths since the caller
	 * assumes it will be free'd when this item is dequeued.
	 */
	free(data, M_BUS);
	return;
}

void
devctl_queue_data(char *data)
{

	devctl_queue_data_f(data, M_NOWAIT);
}

/**
 * @brief Send a 'notification' to userland, using standard ways
 */
void
devctl_notify_f(const char *system, const char *subsystem, const char *type,
    const char *data, int flags)
{
	int len = 0;
	char *msg;

	if (system == NULL)
		return;		/* BOGUS!  Must specify system. */
	if (subsystem == NULL)
		return;		/* BOGUS!  Must specify subsystem. */
	if (type == NULL)
		return;		/* BOGUS!  Must specify type. */
	len += strlen(" system=") + strlen(system);
	len += strlen(" subsystem=") + strlen(subsystem);
	len += strlen(" type=") + strlen(type);
	/* add in the data message plus newline. */
	if (data != NULL)
		len += strlen(data);
	len += 3;	/* '!', '\n', and NUL */
	msg = malloc(len, M_BUS, flags);
	if (msg == NULL)
		return;		/* Drop it on the floor */
	if (data != NULL)
		snprintf(msg, len, "!system=%s subsystem=%s type=%s %s\n",
		    system, subsystem, type, data);
	else
		snprintf(msg, len, "!system=%s subsystem=%s type=%s\n",
		    system, subsystem, type);
	devctl_queue_data_f(msg, flags);
}

void
devctl_notify(const char *system, const char *subsystem, const char *type,
    const char *data)
{

	devctl_notify_f(system, subsystem, type, data, M_NOWAIT);
}

/*
 * Common routine that tries to make sending messages as easy as possible.
 * We allocate memory for the data, copy strings into that, but do not
 * free it unless there's an error.  The dequeue part of the driver should
 * free the data.  We don't send data when the device is disabled.  We do
 * send data, even when we have no listeners, because we wish to avoid
 * races relating to startup and restart of listening applications.
 *
 * devaddq is designed to string together the type of event, with the
 * object of that event, plus the plug and play info and location info
 * for that event.  This is likely most useful for devices, but less
 * useful for other consumers of this interface.  Those should use
 * the devctl_queue_data() interface instead.
 */
static void
devaddq(const char *type, const char *what, device_t dev)
{
	char *data = NULL;
	char *loc = NULL;
	char *pnp = NULL;
	const char *parstr;

	if (!devctl_queue_length)/* Rare race, but lost races safely discard */
		return;
	data = malloc(1024, M_BUS, M_NOWAIT);
	if (data == NULL)
		goto bad;

	/* get the bus specific location of this device */
	loc = malloc(1024, M_BUS, M_NOWAIT);
	if (loc == NULL)
		goto bad;
	*loc = '\0';
	bus_child_location_str(dev, loc, 1024);

	/* Get the bus specific pnp info of this device */
	pnp = malloc(1024, M_BUS, M_NOWAIT);
	if (pnp == NULL)
		goto bad;
	*pnp = '\0';
	bus_child_pnpinfo_str(dev, pnp, 1024);

	/* Get the parent of this device, or / if high enough in the tree. */
	if (device_get_parent(dev) == NULL)
		parstr = ".";	/* Or '/' ? */
	else
		parstr = device_get_nameunit(device_get_parent(dev));
	/* String it all together. */
	snprintf(data, 1024, "%s%s at %s %s on %s\n", type, what, loc, pnp,
	  parstr);
	free(loc, M_BUS);
	free(pnp, M_BUS);
	devctl_queue_data(data);
	return;
bad:
	free(pnp, M_BUS);
	free(loc, M_BUS);
	free(data, M_BUS);
	return;
}

/*
 * A device was added to the tree.  We are called just after it successfully
 * attaches (that is, probe and attach success for this device).  No call
 * is made if a device is merely parented into the tree.  See devnomatch
 * if probe fails.  If attach fails, no notification is sent (but maybe
 * we should have a different message for this).
 */
static void
devadded(device_t dev)
{
	devaddq("+", device_get_nameunit(dev), dev);
}

/*
 * A device was removed from the tree.  We are called just before this
 * happens.
 */
static void
devremoved(device_t dev)
{
	devaddq("-", device_get_nameunit(dev), dev);
}

/*
 * Called when there's no match for this device.  This is only called
 * the first time that no match happens, so we don't keep getting this
 * message.  Should that prove to be undesirable, we can change it.
 * This is called when all drivers that can attach to a given bus
 * decline to accept this device.  Other errors may not be detected.
 */
static void
devnomatch(device_t dev)
{
	devaddq("?", "", dev);
}

static int
sysctl_devctl_disable(SYSCTL_HANDLER_ARGS)
{
	struct dev_event_info *n1;
	int dis, error;

	dis = (devctl_queue_length == 0);
	error = sysctl_handle_int(oidp, &dis, 0, req);
	if (error || !req->newptr)
		return (error);
	if (mtx_initialized(&devsoftc.mtx))
		mtx_lock(&devsoftc.mtx);
	if (dis) {
		while (!TAILQ_EMPTY(&devsoftc.devq)) {
			n1 = TAILQ_FIRST(&devsoftc.devq);
			TAILQ_REMOVE(&devsoftc.devq, n1, dei_link);
			free(n1->dei_data, M_BUS);
			free(n1, M_BUS);
		}
		devsoftc.queued = 0;
		devctl_queue_length = 0;
	} else {
		devctl_queue_length = DEVCTL_DEFAULT_QUEUE_LEN;
	}
	if (mtx_initialized(&devsoftc.mtx))
		mtx_unlock(&devsoftc.mtx);
	return (0);
}

static int
sysctl_devctl_queue(SYSCTL_HANDLER_ARGS)
{
	struct dev_event_info *n1;
	int q, error;

	q = devctl_queue_length;
	error = sysctl_handle_int(oidp, &q, 0, req);
	if (error || !req->newptr)
		return (error);
	if (q < 0)
		return (EINVAL);
	if (mtx_initialized(&devsoftc.mtx))
		mtx_lock(&devsoftc.mtx);
	devctl_queue_length = q;
	while (devsoftc.queued > devctl_queue_length) {
		n1 = TAILQ_FIRST(&devsoftc.devq);
		TAILQ_REMOVE(&devsoftc.devq, n1, dei_link);
		free(n1->dei_data, M_BUS);
		free(n1, M_BUS);
		devsoftc.queued--;
	}
	if (mtx_initialized(&devsoftc.mtx))
		mtx_unlock(&devsoftc.mtx);
	return (0);
}

/**
 * @brief safely quotes strings that might have double quotes in them.
 *
 * The devctl protocol relies on quoted strings having matching quotes.
 * This routine quotes any internal quotes so the resulting string
 * is safe to pass to snprintf to construct, for example pnp info strings.
 * Strings are always terminated with a NUL, but may be truncated if longer
 * than @p len bytes after quotes.
 *
 * @param sb	sbuf to place the characters into
 * @param src	Original buffer.
 */
void
devctl_safe_quote_sb(struct sbuf *sb, const char *src)
{

	while (*src != '\0') {
		if (*src == '"' || *src == '\\')
			sbuf_putc(sb, '\\');
		sbuf_putc(sb, *src++);
	}
}

/* End of /dev/devctl code */

static TAILQ_HEAD(,device)	bus_data_devices;
static int bus_data_generation = 1;

static kobj_method_t null_methods[] = {
	KOBJMETHOD_END
};

DEFINE_CLASS(null, null_methods, 0);

/*
 * Bus pass implementation
 */

static driver_list_t passes = TAILQ_HEAD_INITIALIZER(passes);
int bus_current_pass = BUS_PASS_ROOT;

/**
 * @internal
 * @brief Register the pass level of a new driver attachment
 *
 * Register a new driver attachment's pass level.  If no driver
 * attachment with the same pass level has been added, then @p new
 * will be added to the global passes list.
 *
 * @param new		the new driver attachment
 */
static void
driver_register_pass(struct driverlink *new)
{
	struct driverlink *dl;

	/* We only consider pass numbers during boot. */
	if (bus_current_pass == BUS_PASS_DEFAULT)
		return;

	/*
	 * Walk the passes list.  If we already know about this pass
	 * then there is nothing to do.  If we don't, then insert this
	 * driver link into the list.
	 */
	TAILQ_FOREACH(dl, &passes, passlink) {
		if (dl->pass < new->pass)
			continue;
		if (dl->pass == new->pass)
			return;
		TAILQ_INSERT_BEFORE(dl, new, passlink);
		return;
	}
	TAILQ_INSERT_TAIL(&passes, new, passlink);
}

/**
 * @brief Raise the current bus pass
 *
 * Raise the current bus pass level to @p pass.  Call the BUS_NEW_PASS()
 * method on the root bus to kick off a new device tree scan for each
 * new pass level that has at least one driver.
 */
void
bus_set_pass(int pass)
{
	struct driverlink *dl;

	if (bus_current_pass > pass)
		panic("Attempt to lower bus pass level");

	TAILQ_FOREACH(dl, &passes, passlink) {
		/* Skip pass values below the current pass level. */
		if (dl->pass <= bus_current_pass)
			continue;

		/*
		 * Bail once we hit a driver with a pass level that is
		 * too high.
		 */
		if (dl->pass > pass)
			break;

		/*
		 * Raise the pass level to the next level and rescan
		 * the tree.
		 */
		bus_current_pass = dl->pass;
		BUS_NEW_PASS(root_bus);
	}

	/*
	 * If there isn't a driver registered for the requested pass,
	 * then bus_current_pass might still be less than 'pass'.  Set
	 * it to 'pass' in that case.
	 */
	if (bus_current_pass < pass)
		bus_current_pass = pass;
	KASSERT(bus_current_pass == pass, ("Failed to update bus pass level"));
}

/*
 * Devclass implementation
 */

static devclass_list_t devclasses = TAILQ_HEAD_INITIALIZER(devclasses);

/**
 * @internal
 * @brief Find or create a device class
 *
 * If a device class with the name @p classname exists, return it,
 * otherwise if @p create is non-zero create and return a new device
 * class.
 *
 * If @p parentname is non-NULL, the parent of the devclass is set to
 * the devclass of that name.
 *
 * @param classname	the devclass name to find or create
 * @param parentname	the parent devclass name or @c NULL
 * @param create	non-zero to create a devclass
 */
static devclass_t
devclass_find_internal(const char *classname, const char *parentname,
		       int create)
{
	devclass_t dc;

	PDEBUG(("looking for %s", classname));
	if (!classname)
		return (NULL);

	TAILQ_FOREACH(dc, &devclasses, link) {
		if (!strcmp(dc->name, classname))
			break;
	}

	if (create && !dc) {
		PDEBUG(("creating %s", classname));
		dc = malloc(sizeof(struct devclass) + strlen(classname) + 1,
		    M_BUS, M_NOWAIT | M_ZERO);
		if (!dc)
			return (NULL);
		dc->parent = NULL;
		dc->name = (char*) (dc + 1);
		strcpy(dc->name, classname);
		TAILQ_INIT(&dc->drivers);
		TAILQ_INSERT_TAIL(&devclasses, dc, link);

		bus_data_generation_update();
	}

	/*
	 * If a parent class is specified, then set that as our parent so
	 * that this devclass will support drivers for the parent class as
	 * well.  If the parent class has the same name don't do this though
	 * as it creates a cycle that can trigger an infinite loop in
	 * device_probe_child() if a device exists for which there is no
	 * suitable driver.
	 */
	if (parentname && dc && !dc->parent &&
	    strcmp(classname, parentname) != 0) {
		dc->parent = devclass_find_internal(parentname, NULL, TRUE);
		dc->parent->flags |= DC_HAS_CHILDREN;
	}

	return (dc);
}

/**
 * @brief Create a device class
 *
 * If a device class with the name @p classname exists, return it,
 * otherwise create and return a new device class.
 *
 * @param classname	the devclass name to find or create
 */
devclass_t
devclass_create(const char *classname)
{
	return (devclass_find_internal(classname, NULL, TRUE));
}

/**
 * @brief Find a device class
 *
 * If a device class with the name @p classname exists, return it,
 * otherwise return @c NULL.
 *
 * @param classname	the devclass name to find
 */
devclass_t
devclass_find(const char *classname)
{
	return (devclass_find_internal(classname, NULL, FALSE));
}

/**
 * @brief Register that a device driver has been added to a devclass
 *
 * Register that a device driver has been added to a devclass.  This
 * is called by devclass_add_driver to accomplish the recursive
 * notification of all the children classes of dc, as well as dc.
 * Each layer will have BUS_DRIVER_ADDED() called for all instances of
 * the devclass.
 *
 * We do a full search here of the devclass list at each iteration
 * level to save storing children-lists in the devclass structure.  If
 * we ever move beyond a few dozen devices doing this, we may need to
 * reevaluate...
 *
 * @param dc		the devclass to edit
 * @param driver	the driver that was just added
 */
static void
devclass_driver_added(devclass_t dc, driver_t *driver)
{
	devclass_t parent;
	int i;

	/*
	 * Call BUS_DRIVER_ADDED for any existing buses in this class.
	 */
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i] && device_is_attached(dc->devices[i]))
			BUS_DRIVER_ADDED(dc->devices[i], driver);

	/*
	 * Walk through the children classes.  Since we only keep a
	 * single parent pointer around, we walk the entire list of
	 * devclasses looking for children.  We set the
	 * DC_HAS_CHILDREN flag when a child devclass is created on
	 * the parent, so we only walk the list for those devclasses
	 * that have children.
	 */
	if (!(dc->flags & DC_HAS_CHILDREN))
		return;
	parent = dc;
	TAILQ_FOREACH(dc, &devclasses, link) {
		if (dc->parent == parent)
			devclass_driver_added(dc, driver);
	}
}

/**
 * @brief Add a device driver to a device class
 *
 * Add a device driver to a devclass. This is normally called
 * automatically by DRIVER_MODULE(). The BUS_DRIVER_ADDED() method of
 * all devices in the devclass will be called to allow them to attempt
 * to re-probe any unmatched children.
 *
 * @param dc		the devclass to edit
 * @param driver	the driver to register
 */
int
devclass_add_driver(devclass_t dc, driver_t *driver, int pass, devclass_t *dcp)
{
	driverlink_t dl;
	const char *parentname;

	PDEBUG(("%s", DRIVERNAME(driver)));

	/* Don't allow invalid pass values. */
	if (pass <= BUS_PASS_ROOT)
		return (EINVAL);

	dl = malloc(sizeof *dl, M_BUS, M_NOWAIT|M_ZERO);
	if (!dl)
		return (ENOMEM);

	/*
	 * Compile the driver's methods. Also increase the reference count
	 * so that the class doesn't get freed when the last instance
	 * goes. This means we can safely use static methods and avoids a
	 * double-free in devclass_delete_driver.
	 */
	kobj_class_compile((kobj_class_t) driver);

	/*
	 * If the driver has any base classes, make the
	 * devclass inherit from the devclass of the driver's
	 * first base class. This will allow the system to
	 * search for drivers in both devclasses for children
	 * of a device using this driver.
	 */
	if (driver->baseclasses)
		parentname = driver->baseclasses[0]->name;
	else
		parentname = NULL;
	*dcp = devclass_find_internal(driver->name, parentname, TRUE);

	dl->driver = driver;
	TAILQ_INSERT_TAIL(&dc->drivers, dl, link);
	driver->refs++;		/* XXX: kobj_mtx */
	dl->pass = pass;
	driver_register_pass(dl);

	if (device_frozen) {
		dl->flags |= DL_DEFERRED_PROBE;
	} else {
		devclass_driver_added(dc, driver);
	}
	bus_data_generation_update();
	return (0);
}

/**
 * @brief Register that a device driver has been deleted from a devclass
 *
 * Register that a device driver has been removed from a devclass.
 * This is called by devclass_delete_driver to accomplish the
 * recursive notification of all the children classes of busclass, as
 * well as busclass.  Each layer will attempt to detach the driver
 * from any devices that are children of the bus's devclass.  The function
 * will return an error if a device fails to detach.
 *
 * We do a full search here of the devclass list at each iteration
 * level to save storing children-lists in the devclass structure.  If
 * we ever move beyond a few dozen devices doing this, we may need to
 * reevaluate...
 *
 * @param busclass	the devclass of the parent bus
 * @param dc		the devclass of the driver being deleted
 * @param driver	the driver being deleted
 */
static int
devclass_driver_deleted(devclass_t busclass, devclass_t dc, driver_t *driver)
{
	devclass_t parent;
	device_t dev;
	int error, i;

	/*
	 * Disassociate from any devices.  We iterate through all the
	 * devices in the devclass of the driver and detach any which are
	 * using the driver and which have a parent in the devclass which
	 * we are deleting from.
	 *
	 * Note that since a driver can be in multiple devclasses, we
	 * should not detach devices which are not children of devices in
	 * the affected devclass.
	 *
	 * If we're frozen, we don't generate NOMATCH events. Mark to
	 * generate later.
	 */
	for (i = 0; i < dc->maxunit; i++) {
		if (dc->devices[i]) {
			dev = dc->devices[i];
			if (dev->driver == driver && dev->parent &&
			    dev->parent->devclass == busclass) {
				if ((error = device_detach(dev)) != 0)
					return (error);
				if (device_frozen) {
					dev->flags &= ~DF_DONENOMATCH;
					dev->flags |= DF_NEEDNOMATCH;
				} else {
					BUS_PROBE_NOMATCH(dev->parent, dev);
					devnomatch(dev);
					dev->flags |= DF_DONENOMATCH;
				}
			}
		}
	}

	/*
	 * Walk through the children classes.  Since we only keep a
	 * single parent pointer around, we walk the entire list of
	 * devclasses looking for children.  We set the
	 * DC_HAS_CHILDREN flag when a child devclass is created on
	 * the parent, so we only walk the list for those devclasses
	 * that have children.
	 */
	if (!(busclass->flags & DC_HAS_CHILDREN))
		return (0);
	parent = busclass;
	TAILQ_FOREACH(busclass, &devclasses, link) {
		if (busclass->parent == parent) {
			error = devclass_driver_deleted(busclass, dc, driver);
			if (error)
				return (error);
		}
	}
	return (0);
}

/**
 * @brief Delete a device driver from a device class
 *
 * Delete a device driver from a devclass. This is normally called
 * automatically by DRIVER_MODULE().
 *
 * If the driver is currently attached to any devices,
 * devclass_delete_driver() will first attempt to detach from each
 * device. If one of the detach calls fails, the driver will not be
 * deleted.
 *
 * @param dc		the devclass to edit
 * @param driver	the driver to unregister
 */
int
devclass_delete_driver(devclass_t busclass, driver_t *driver)
{
	devclass_t dc = devclass_find(driver->name);
	driverlink_t dl;
	int error;

	PDEBUG(("%s from devclass %s", driver->name, DEVCLANAME(busclass)));

	if (!dc)
		return (0);

	/*
	 * Find the link structure in the bus' list of drivers.
	 */
	TAILQ_FOREACH(dl, &busclass->drivers, link) {
		if (dl->driver == driver)
			break;
	}

	if (!dl) {
		PDEBUG(("%s not found in %s list", driver->name,
		    busclass->name));
		return (ENOENT);
	}

	error = devclass_driver_deleted(busclass, dc, driver);
	if (error != 0)
		return (error);

	TAILQ_REMOVE(&busclass->drivers, dl, link);
	free(dl, M_BUS);

	/* XXX: kobj_mtx */
	driver->refs--;
	if (driver->refs == 0)
		kobj_class_free((kobj_class_t) driver);

	bus_data_generation_update();
	return (0);
}

/**
 * @brief Quiesces a set of device drivers from a device class
 *
 * Quiesce a device driver from a devclass. This is normally called
 * automatically by DRIVER_MODULE().
 *
 * If the driver is currently attached to any devices,
 * devclass_quiesece_driver() will first attempt to quiesce each
 * device.
 *
 * @param dc		the devclass to edit
 * @param driver	the driver to unregister
 */
static int
devclass_quiesce_driver(devclass_t busclass, driver_t *driver)
{
	devclass_t dc = devclass_find(driver->name);
	driverlink_t dl;
	device_t dev;
	int i;
	int error;

	PDEBUG(("%s from devclass %s", driver->name, DEVCLANAME(busclass)));

	if (!dc)
		return (0);

	/*
	 * Find the link structure in the bus' list of drivers.
	 */
	TAILQ_FOREACH(dl, &busclass->drivers, link) {
		if (dl->driver == driver)
			break;
	}

	if (!dl) {
		PDEBUG(("%s not found in %s list", driver->name,
		    busclass->name));
		return (ENOENT);
	}

	/*
	 * Quiesce all devices.  We iterate through all the devices in
	 * the devclass of the driver and quiesce any which are using
	 * the driver and which have a parent in the devclass which we
	 * are quiescing.
	 *
	 * Note that since a driver can be in multiple devclasses, we
	 * should not quiesce devices which are not children of
	 * devices in the affected devclass.
	 */
	for (i = 0; i < dc->maxunit; i++) {
		if (dc->devices[i]) {
			dev = dc->devices[i];
			if (dev->driver == driver && dev->parent &&
			    dev->parent->devclass == busclass) {
				if ((error = device_quiesce(dev)) != 0)
					return (error);
			}
		}
	}

	return (0);
}

/**
 * @internal
 */
static driverlink_t
devclass_find_driver_internal(devclass_t dc, const char *classname)
{
	driverlink_t dl;

	PDEBUG(("%s in devclass %s", classname, DEVCLANAME(dc)));

	TAILQ_FOREACH(dl, &dc->drivers, link) {
		if (!strcmp(dl->driver->name, classname))
			return (dl);
	}

	PDEBUG(("not found"));
	return (NULL);
}

/**
 * @brief Return the name of the devclass
 */
const char *
devclass_get_name(devclass_t dc)
{
	return (dc->name);
}

/**
 * @brief Find a device given a unit number
 *
 * @param dc		the devclass to search
 * @param unit		the unit number to search for
 *
 * @returns		the device with the given unit number or @c
 *			NULL if there is no such device
 */
device_t
devclass_get_device(devclass_t dc, int unit)
{
	if (dc == NULL || unit < 0 || unit >= dc->maxunit)
		return (NULL);
	return (dc->devices[unit]);
}

/**
 * @brief Find the softc field of a device given a unit number
 *
 * @param dc		the devclass to search
 * @param unit		the unit number to search for
 *
 * @returns		the softc field of the device with the given
 *			unit number or @c NULL if there is no such
 *			device
 */
void *
devclass_get_softc(devclass_t dc, int unit)
{
	device_t dev;

	dev = devclass_get_device(dc, unit);
	if (!dev)
		return (NULL);

	return (device_get_softc(dev));
}

/**
 * @brief Get a list of devices in the devclass
 *
 * An array containing a list of all the devices in the given devclass
 * is allocated and returned in @p *devlistp. The number of devices
 * in the array is returned in @p *devcountp. The caller should free
 * the array using @c free(p, M_TEMP), even if @p *devcountp is 0.
 *
 * @param dc		the devclass to examine
 * @param devlistp	points at location for array pointer return
 *			value
 * @param devcountp	points at location for array size return value
 *
 * @retval 0		success
 * @retval ENOMEM	the array allocation failed
 */
int
devclass_get_devices(devclass_t dc, device_t **devlistp, int *devcountp)
{
	int count, i;
	device_t *list;

	count = devclass_get_count(dc);
	list = malloc(count * sizeof(device_t), M_TEMP, M_NOWAIT|M_ZERO);
	if (!list)
		return (ENOMEM);

	count = 0;
	for (i = 0; i < dc->maxunit; i++) {
		if (dc->devices[i]) {
			list[count] = dc->devices[i];
			count++;
		}
	}

	*devlistp = list;
	*devcountp = count;

	return (0);
}

/**
 * @brief Get a list of drivers in the devclass
 *
 * An array containing a list of pointers to all the drivers in the
 * given devclass is allocated and returned in @p *listp.  The number
 * of drivers in the array is returned in @p *countp. The caller should
 * free the array using @c free(p, M_TEMP).
 *
 * @param dc		the devclass to examine
 * @param listp		gives location for array pointer return value
 * @param countp	gives location for number of array elements
 *			return value
 *
 * @retval 0		success
 * @retval ENOMEM	the array allocation failed
 */
int
devclass_get_drivers(devclass_t dc, driver_t ***listp, int *countp)
{
	driverlink_t dl;
	driver_t **list;
	int count;

	count = 0;
	TAILQ_FOREACH(dl, &dc->drivers, link)
		count++;
	list = malloc(count * sizeof(driver_t *), M_TEMP, M_NOWAIT);
	if (list == NULL)
		return (ENOMEM);

	count = 0;
	TAILQ_FOREACH(dl, &dc->drivers, link) {
		list[count] = dl->driver;
		count++;
	}
	*listp = list;
	*countp = count;

	return (0);
}

/**
 * @brief Get the number of devices in a devclass
 *
 * @param dc		the devclass to examine
 */
int
devclass_get_count(devclass_t dc)
{
	int count, i;

	count = 0;
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i])
			count++;
	return (count);
}

/**
 * @brief Get the maximum unit number used in a devclass
 *
 * Note that this is one greater than the highest currently-allocated
 * unit.  If a null devclass_t is passed in, -1 is returned to indicate
 * that not even the devclass has been allocated yet.
 *
 * @param dc		the devclass to examine
 */
int
devclass_get_maxunit(devclass_t dc)
{
	if (dc == NULL)
		return (-1);
	return (dc->maxunit);
}

/**
 * @brief Find a free unit number in a devclass
 *
 * This function searches for the first unused unit number greater
 * that or equal to @p unit.
 *
 * @param dc		the devclass to examine
 * @param unit		the first unit number to check
 */
int
devclass_find_free_unit(devclass_t dc, int unit)
{
	if (dc == NULL)
		return (unit);
	while (unit < dc->maxunit && dc->devices[unit] != NULL)
		unit++;
	return (unit);
}

/**
 * @brief Set the parent of a devclass
 *
 * The parent class is normally initialised automatically by
 * DRIVER_MODULE().
 *
 * @param dc		the devclass to edit
 * @param pdc		the new parent devclass
 */
void
devclass_set_parent(devclass_t dc, devclass_t pdc)
{
	dc->parent = pdc;
}

/**
 * @brief Get the parent of a devclass
 *
 * @param dc		the devclass to examine
 */
devclass_t
devclass_get_parent(devclass_t dc)
{
	return (dc->parent);
}

struct sysctl_ctx_list *
devclass_get_sysctl_ctx(devclass_t dc)
{
	return (&dc->sysctl_ctx);
}

struct sysctl_oid *
devclass_get_sysctl_tree(devclass_t dc)
{
	return (dc->sysctl_tree);
}

/**
 * @internal
 * @brief Allocate a unit number
 *
 * On entry, @p *unitp is the desired unit number (or @c -1 if any
 * will do). The allocated unit number is returned in @p *unitp.

 * @param dc		the devclass to allocate from
 * @param unitp		points at the location for the allocated unit
 *			number
 *
 * @retval 0		success
 * @retval EEXIST	the requested unit number is already allocated
 * @retval ENOMEM	memory allocation failure
 */
static int
devclass_alloc_unit(devclass_t dc, device_t dev, int *unitp)
{
	const char *s;
	int unit = *unitp;

	PDEBUG(("unit %d in devclass %s", unit, DEVCLANAME(dc)));

	/* Ask the parent bus if it wants to wire this device. */
	if (unit == -1)
		BUS_HINT_DEVICE_UNIT(device_get_parent(dev), dev, dc->name,
		    &unit);

	/* If we were given a wired unit number, check for existing device */
	/* XXX imp XXX */
	if (unit != -1) {
		if (unit >= 0 && unit < dc->maxunit &&
		    dc->devices[unit] != NULL) {
			if (bootverbose)
				printf("%s: %s%d already exists; skipping it\n",
				    dc->name, dc->name, *unitp);
			return (EEXIST);
		}
	} else {
		/* Unwired device, find the next available slot for it */
		unit = 0;
		for (unit = 0;; unit++) {
			/* If there is an "at" hint for a unit then skip it. */
			if (resource_string_value(dc->name, unit, "at", &s) ==
			    0)
				continue;

			/* If this device slot is already in use, skip it. */
			if (unit < dc->maxunit && dc->devices[unit] != NULL)
				continue;

			break;
		}
	}

	/*
	 * We've selected a unit beyond the length of the table, so let's
	 * extend the table to make room for all units up to and including
	 * this one.
	 */
	if (unit >= dc->maxunit) {
		device_t *newlist, *oldlist;
		int newsize;

		oldlist = dc->devices;
		newsize = roundup((unit + 1), MINALLOCSIZE / sizeof(device_t));
		newlist = malloc(sizeof(device_t) * newsize, M_BUS, M_NOWAIT);
		if (!newlist)
			return (ENOMEM);
		if (oldlist != NULL)
			bcopy(oldlist, newlist, sizeof(device_t) * dc->maxunit);
		bzero(newlist + dc->maxunit,
		    sizeof(device_t) * (newsize - dc->maxunit));
		dc->devices = newlist;
		dc->maxunit = newsize;
		if (oldlist != NULL)
			free(oldlist, M_BUS);
	}
	PDEBUG(("now: unit %d in devclass %s", unit, DEVCLANAME(dc)));

	*unitp = unit;
	return (0);
}

/**
 * @internal
 * @brief Add a device to a devclass
 *
 * A unit number is allocated for the device (using the device's
 * preferred unit number if any) and the device is registered in the
 * devclass. This allows the device to be looked up by its unit
 * number, e.g. by decoding a dev_t minor number.
 *
 * @param dc		the devclass to add to
 * @param dev		the device to add
 *
 * @retval 0		success
 * @retval EEXIST	the requested unit number is already allocated
 * @retval ENOMEM	memory allocation failure
 */
static int
devclass_add_device(devclass_t dc, device_t dev)
{
	int buflen, error;

	PDEBUG(("%s in devclass %s", DEVICENAME(dev), DEVCLANAME(dc)));

	buflen = snprintf(NULL, 0, "%s%d$", dc->name, INT_MAX);
	if (buflen < 0)
		return (ENOMEM);
	dev->nameunit = malloc(buflen, M_BUS, M_NOWAIT|M_ZERO);
	if (!dev->nameunit)
		return (ENOMEM);

	if ((error = devclass_alloc_unit(dc, dev, &dev->unit)) != 0) {
		free(dev->nameunit, M_BUS);
		dev->nameunit = NULL;
		return (error);
	}
	dc->devices[dev->unit] = dev;
	dev->devclass = dc;
	snprintf(dev->nameunit, buflen, "%s%d", dc->name, dev->unit);

	return (0);
}

/**
 * @internal
 * @brief Delete a device from a devclass
 *
 * The device is removed from the devclass's device list and its unit
 * number is freed.

 * @param dc		the devclass to delete from
 * @param dev		the device to delete
 *
 * @retval 0		success
 */
static int
devclass_delete_device(devclass_t dc, device_t dev)
{
	if (!dc || !dev)
		return (0);

	PDEBUG(("%s in devclass %s", DEVICENAME(dev), DEVCLANAME(dc)));

	if (dev->devclass != dc || dc->devices[dev->unit] != dev)
		panic("devclass_delete_device: inconsistent device class");
	dc->devices[dev->unit] = NULL;
	if (dev->flags & DF_WILDCARD)
		dev->unit = -1;
	dev->devclass = NULL;
	free(dev->nameunit, M_BUS);
	dev->nameunit = NULL;

	return (0);
}

/**
 * @internal
 * @brief Make a new device and add it as a child of @p parent
 *
 * @param parent	the parent of the new device
 * @param name		the devclass name of the new device or @c NULL
 *			to leave the devclass unspecified
 * @parem unit		the unit number of the new device of @c -1 to
 *			leave the unit number unspecified
 *
 * @returns the new device
 */
static device_t
make_device(device_t parent, const char *name, int unit)
{
	device_t dev;
	devclass_t dc;

	PDEBUG(("%s at %s as unit %d", name, DEVICENAME(parent), unit));

	if (name) {
		dc = devclass_find_internal(name, NULL, TRUE);
		if (!dc) {
			printf("make_device: can't find device class %s\n",
			    name);
			return (NULL);
		}
	} else {
		dc = NULL;
	}

	dev = malloc(sizeof(*dev), M_BUS, M_NOWAIT|M_ZERO);
	if (!dev)
		return (NULL);

	dev->parent = parent;
	TAILQ_INIT(&dev->children);
	kobj_init((kobj_t) dev, &null_class);
	dev->driver = NULL;
	dev->devclass = NULL;
	dev->unit = unit;
	dev->nameunit = NULL;
	dev->desc = NULL;
	dev->busy = 0;
	dev->devflags = 0;
	dev->flags = DF_ENABLED;
	dev->order = 0;
	if (unit == -1)
		dev->flags |= DF_WILDCARD;
	if (name) {
		dev->flags |= DF_FIXEDCLASS;
		if (devclass_add_device(dc, dev)) {
			kobj_delete((kobj_t) dev, M_BUS);
			return (NULL);
		}
	}
	if (parent != NULL && device_has_quiet_children(parent))
		dev->flags |= DF_QUIET | DF_QUIET_CHILDREN;
	dev->ivars = NULL;
	dev->softc = NULL;

	dev->state = DS_NOTPRESENT;

	TAILQ_INSERT_TAIL(&bus_data_devices, dev, devlink);
	bus_data_generation_update();

	return (dev);
}

/**
 * @internal
 * @brief Print a description of a device.
 */
static int
device_print_child(device_t dev, device_t child)
{
	int retval = 0;

	if (device_is_alive(child))
		retval += BUS_PRINT_CHILD(dev, child);
	else
		retval += device_printf(child, " not found\n");

	return (retval);
}

/**
 * @brief Create a new device
 *
 * This creates a new device and adds it as a child of an existing
 * parent device. The new device will be added after the last existing
 * child with order zero.
 *
 * @param dev		the device which will be the parent of the
 *			new child device
 * @param name		devclass name for new device or @c NULL if not
 *			specified
 * @param unit		unit number for new device or @c -1 if not
 *			specified
 *
 * @returns		the new device
 */
device_t
device_add_child(device_t dev, const char *name, int unit)
{
	return (device_add_child_ordered(dev, 0, name, unit));
}

/**
 * @brief Create a new device
 *
 * This creates a new device and adds it as a child of an existing
 * parent device. The new device will be added after the last existing
 * child with the same order.
 *
 * @param dev		the device which will be the parent of the
 *			new child device
 * @param order		a value which is used to partially sort the
 *			children of @p dev - devices created using
 *			lower values of @p order appear first in @p
 *			dev's list of children
 * @param name		devclass name for new device or @c NULL if not
 *			specified
 * @param unit		unit number for new device or @c -1 if not
 *			specified
 *
 * @returns		the new device
 */
device_t
device_add_child_ordered(device_t dev, u_int order, const char *name, int unit)
{
	device_t child;
	device_t place;

	PDEBUG(("%s at %s with order %u as unit %d",
	    name, DEVICENAME(dev), order, unit));
	KASSERT(name != NULL || unit == -1,
	    ("child device with wildcard name and specific unit number"));

	child = make_device(dev, name, unit);
	if (child == NULL)
		return (child);
	child->order = order;

	TAILQ_FOREACH(place, &dev->children, link) {
		if (place->order > order)
			break;
	}

	if (place) {
		/*
		 * The device 'place' is the first device whose order is
		 * greater than the new child.
		 */
		TAILQ_INSERT_BEFORE(place, child, link);
	} else {
		/*
		 * The new child's order is greater or equal to the order of
		 * any existing device. Add the child to the tail of the list.
		 */
		TAILQ_INSERT_TAIL(&dev->children, child, link);
	}

	bus_data_generation_update();
	return (child);
}

/**
 * @brief Delete a device
 *
 * This function deletes a device along with all of its children. If
 * the device currently has a driver attached to it, the device is
 * detached first using device_detach().
 *
 * @param dev		the parent device
 * @param child		the device to delete
 *
 * @retval 0		success
 * @retval non-zero	a unit error code describing the error
 */
int
device_delete_child(device_t dev, device_t child)
{
	int error;
	device_t grandchild;

	PDEBUG(("%s from %s", DEVICENAME(child), DEVICENAME(dev)));

	/* detach parent before deleting children, if any */
	if ((error = device_detach(child)) != 0)
		return (error);
	
	/* remove children second */
	while ((grandchild = TAILQ_FIRST(&child->children)) != NULL) {
		error = device_delete_child(child, grandchild);
		if (error)
			return (error);
	}

	if (child->devclass)
		devclass_delete_device(child->devclass, child);
	if (child->parent)
		BUS_CHILD_DELETED(dev, child);
	TAILQ_REMOVE(&dev->children, child, link);
	TAILQ_REMOVE(&bus_data_devices, child, devlink);
	kobj_delete((kobj_t) child, M_BUS);

	bus_data_generation_update();
	return (0);
}

/**
 * @brief Delete all children devices of the given device, if any.
 *
 * This function deletes all children devices of the given device, if
 * any, using the device_delete_child() function for each device it
 * finds. If a child device cannot be deleted, this function will
 * return an error code.
 *
 * @param dev		the parent device
 *
 * @retval 0		success
 * @retval non-zero	a device would not detach
 */
int
device_delete_children(device_t dev)
{
	device_t child;
	int error;

	PDEBUG(("Deleting all children of %s", DEVICENAME(dev)));

	error = 0;

	while ((child = TAILQ_FIRST(&dev->children)) != NULL) {
		error = device_delete_child(dev, child);
		if (error) {
			PDEBUG(("Failed deleting %s", DEVICENAME(child)));
			break;
		}
	}
	return (error);
}

/**
 * @brief Find a device given a unit number
 *
 * This is similar to devclass_get_devices() but only searches for
 * devices which have @p dev as a parent.
 *
 * @param dev		the parent device to search
 * @param unit		the unit number to search for.  If the unit is -1,
 *			return the first child of @p dev which has name
 *			@p classname (that is, the one with the lowest unit.)
 *
 * @returns		the device with the given unit number or @c
 *			NULL if there is no such device
 */
device_t
device_find_child(device_t dev, const char *classname, int unit)
{
	devclass_t dc;
	device_t child;

	dc = devclass_find(classname);
	if (!dc)
		return (NULL);

	if (unit != -1) {
		child = devclass_get_device(dc, unit);
		if (child && child->parent == dev)
			return (child);
	} else {
		for (unit = 0; unit < devclass_get_maxunit(dc); unit++) {
			child = devclass_get_device(dc, unit);
			if (child && child->parent == dev)
				return (child);
		}
	}
	return (NULL);
}

/**
 * @internal
 */
static driverlink_t
first_matching_driver(devclass_t dc, device_t dev)
{
	if (dev->devclass)
		return (devclass_find_driver_internal(dc, dev->devclass->name));
	return (TAILQ_FIRST(&dc->drivers));
}

/**
 * @internal
 */
static driverlink_t
next_matching_driver(devclass_t dc, device_t dev, driverlink_t last)
{
	if (dev->devclass) {
		driverlink_t dl;
		for (dl = TAILQ_NEXT(last, link); dl; dl = TAILQ_NEXT(dl, link))
			if (!strcmp(dev->devclass->name, dl->driver->name))
				return (dl);
		return (NULL);
	}
	return (TAILQ_NEXT(last, link));
}

/**
 * @internal
 */
int
device_probe_child(device_t dev, device_t child)
{
	devclass_t dc;
	driverlink_t best = NULL;
	driverlink_t dl;
	int result, pri = 0;
	int hasclass = (child->devclass != NULL);

	GIANT_REQUIRED;

	dc = dev->devclass;
	if (!dc)
		panic("device_probe_child: parent device has no devclass");

	/*
	 * If the state is already probed, then return.  However, don't
	 * return if we can rebid this object.
	 */
	if (child->state == DS_ALIVE && (child->flags & DF_REBID) == 0)
		return (0);

	for (; dc; dc = dc->parent) {
		for (dl = first_matching_driver(dc, child);
		     dl;
		     dl = next_matching_driver(dc, child, dl)) {
			/* If this driver's pass is too high, then ignore it. */
			if (dl->pass > bus_current_pass)
				continue;

			PDEBUG(("Trying %s", DRIVERNAME(dl->driver)));
			result = device_set_driver(child, dl->driver);
			if (result == ENOMEM)
				return (result);
			else if (result != 0)
				continue;
			if (!hasclass) {
				if (device_set_devclass(child,
				    dl->driver->name) != 0) {
					char const * devname =
					    device_get_name(child);
					if (devname == NULL)
						devname = "(unknown)";
					printf("driver bug: Unable to set "
					    "devclass (class: %s "
					    "devname: %s)\n",
					    dl->driver->name,
					    devname);
					(void)device_set_driver(child, NULL);
					continue;
				}
			}

			/* Fetch any flags for the device before probing. */
			resource_int_value(dl->driver->name, child->unit,
			    "flags", &child->devflags);

			result = DEVICE_PROBE(child);

			/* Reset flags and devclass before the next probe. */
			child->devflags = 0;
			if (!hasclass)
				(void)device_set_devclass(child, NULL);

			/*
			 * If the driver returns SUCCESS, there can be
			 * no higher match for this device.
			 */
			if (result == 0) {
				best = dl;
				pri = 0;
				break;
			}

			/*
			 * Reset DF_QUIET in case this driver doesn't
			 * end up as the best driver.
			 */
			device_verbose(child);

			/*
			 * Probes that return BUS_PROBE_NOWILDCARD or lower
			 * only match on devices whose driver was explicitly
			 * specified.
			 */
			if (result <= BUS_PROBE_NOWILDCARD &&
			    !(child->flags & DF_FIXEDCLASS)) {
				result = ENXIO;
			}

			/*
			 * The driver returned an error so it
			 * certainly doesn't match.
			 */
			if (result > 0) {
				(void)device_set_driver(child, NULL);
				continue;
			}

			/*
			 * A priority lower than SUCCESS, remember the
			 * best matching driver. Initialise the value
			 * of pri for the first match.
			 */
			if (best == NULL || result > pri) {
				best = dl;
				pri = result;
				continue;
			}
		}
		/*
		 * If we have an unambiguous match in this devclass,
		 * don't look in the parent.
		 */
		if (best && pri == 0)
			break;
	}

	/*
	 * If we found a driver, change state and initialise the devclass.
	 */
	/* XXX What happens if we rebid and got no best? */
	if (best) {
		/*
		 * If this device was attached, and we were asked to
		 * rescan, and it is a different driver, then we have
		 * to detach the old driver and reattach this new one.
		 * Note, we don't have to check for DF_REBID here
		 * because if the state is > DS_ALIVE, we know it must
		 * be.
		 *
		 * This assumes that all DF_REBID drivers can have
		 * their probe routine called at any time and that
		 * they are idempotent as well as completely benign in
		 * normal operations.
		 *
		 * We also have to make sure that the detach
		 * succeeded, otherwise we fail the operation (or
		 * maybe it should just fail silently?  I'm torn).
		 */
		if (child->state > DS_ALIVE && best->driver != child->driver)
			if ((result = device_detach(dev)) != 0)
				return (result);

		/* Set the winning driver, devclass, and flags. */
		if (!child->devclass) {
			result = device_set_devclass(child, best->driver->name);
			if (result != 0)
				return (result);
		}
		result = device_set_driver(child, best->driver);
		if (result != 0)
			return (result);
		resource_int_value(best->driver->name, child->unit,
		    "flags", &child->devflags);

		if (pri < 0) {
			/*
			 * A bit bogus. Call the probe method again to make
			 * sure that we have the right description.
			 */
			DEVICE_PROBE(child);
#if 0
			child->flags |= DF_REBID;
#endif
		} else
			child->flags &= ~DF_REBID;
		child->state = DS_ALIVE;

		bus_data_generation_update();
		return (0);
	}

	return (ENXIO);
}

/**
 * @brief Return the parent of a device
 */
device_t
device_get_parent(device_t dev)
{
	return (dev->parent);
}

/**
 * @brief Get a list of children of a device
 *
 * An array containing a list of all the children of the given device
 * is allocated and returned in @p *devlistp. The number of devices
 * in the array is returned in @p *devcountp. The caller should free
 * the array using @c free(p, M_TEMP).
 *
 * @param dev		the device to examine
 * @param devlistp	points at location for array pointer return
 *			value
 * @param devcountp	points at location for array size return value
 *
 * @retval 0		success
 * @retval ENOMEM	the array allocation failed
 */
int
device_get_children(device_t dev, device_t **devlistp, int *devcountp)
{
	int count;
	device_t child;
	device_t *list;

	count = 0;
	TAILQ_FOREACH(child, &dev->children, link) {
		count++;
	}
	if (count == 0) {
		*devlistp = NULL;
		*devcountp = 0;
		return (0);
	}

	list = malloc(count * sizeof(device_t), M_TEMP, M_NOWAIT|M_ZERO);
	if (!list)
		return (ENOMEM);

	count = 0;
	TAILQ_FOREACH(child, &dev->children, link) {
		list[count] = child;
		count++;
	}

	*devlistp = list;
	*devcountp = count;

	return (0);
}

/**
 * @brief Return the current driver for the device or @c NULL if there
 * is no driver currently attached
 */
driver_t *
device_get_driver(device_t dev)
{
	return (dev->driver);
}

/**
 * @brief Return the current devclass for the device or @c NULL if
 * there is none.
 */
devclass_t
device_get_devclass(device_t dev)
{
	return (dev->devclass);
}

/**
 * @brief Return the name of the device's devclass or @c NULL if there
 * is none.
 */
const char *
device_get_name(device_t dev)
{
	if (dev != NULL && dev->devclass)
		return (devclass_get_name(dev->devclass));
	return (NULL);
}

/**
 * @brief Return a string containing the device's devclass name
 * followed by an ascii representation of the device's unit number
 * (e.g. @c "foo2").
 */
const char *
device_get_nameunit(device_t dev)
{
	return (dev->nameunit);
}

/**
 * @brief Return the device's unit number.
 */
int
device_get_unit(device_t dev)
{
	return (dev->unit);
}

/**
 * @brief Return the device's description string
 */
const char *
device_get_desc(device_t dev)
{
	return (dev->desc);
}

/**
 * @brief Return the device's flags
 */
uint32_t
device_get_flags(device_t dev)
{
	return (dev->devflags);
}

struct sysctl_ctx_list *
device_get_sysctl_ctx(device_t dev)
{
	return (&dev->sysctl_ctx);
}

struct sysctl_oid *
device_get_sysctl_tree(device_t dev)
{
	return (dev->sysctl_tree);
}

/**
 * @brief Print the name of the device followed by a colon and a space
 *
 * @returns the number of characters printed
 */
int
device_print_prettyname(device_t dev)
{
	const char *name = device_get_name(dev);

	if (name == NULL)
		return (printf("unknown: "));
	return (printf("%s%d: ", name, device_get_unit(dev)));
}

/**
 * @brief Print the name of the device followed by a colon, a space
 * and the result of calling vprintf() with the value of @p fmt and
 * the following arguments.
 *
 * @returns the number of characters printed
 */
int
device_printf(device_t dev, const char * fmt, ...)
{
	va_list ap;
	int retval;

	retval = device_print_prettyname(dev);
	va_start(ap, fmt);
	retval += vprintf(fmt, ap);
	va_end(ap);
	return (retval);
}

/**
 * @internal
 */
static void
device_set_desc_internal(device_t dev, const char* desc, int copy)
{
	if (dev->desc && (dev->flags & DF_DESCMALLOCED)) {
		free(dev->desc, M_BUS);
		dev->flags &= ~DF_DESCMALLOCED;
		dev->desc = NULL;
	}

	if (copy && desc) {
		dev->desc = malloc(strlen(desc) + 1, M_BUS, M_NOWAIT);
		if (dev->desc) {
			strcpy(dev->desc, desc);
			dev->flags |= DF_DESCMALLOCED;
		}
	} else {
		/* Avoid a -Wcast-qual warning */
		dev->desc = (char *)(uintptr_t) desc;
	}

	bus_data_generation_update();
}

/**
 * @brief Set the device's description
 *
 * The value of @c desc should be a string constant that will not
 * change (at least until the description is changed in a subsequent
 * call to device_set_desc() or device_set_desc_copy()).
 */
void
device_set_desc(device_t dev, const char* desc)
{
	device_set_desc_internal(dev, desc, FALSE);
}

/**
 * @brief Set the device's description
 *
 * The string pointed to by @c desc is copied. Use this function if
 * the device description is generated, (e.g. with sprintf()).
 */
void
device_set_desc_copy(device_t dev, const char* desc)
{
	device_set_desc_internal(dev, desc, TRUE);
}

/**
 * @brief Set the device's flags
 */
void
device_set_flags(device_t dev, uint32_t flags)
{
	dev->devflags = flags;
}

/**
 * @brief Return the device's softc field
 *
 * The softc is allocated and zeroed when a driver is attached, based
 * on the size field of the driver.
 */
void *
device_get_softc(device_t dev)
{
	return (dev->softc);
}

/**
 * @brief Set the device's softc field
 *
 * Most drivers do not need to use this since the softc is allocated
 * automatically when the driver is attached.
 */
void
device_set_softc(device_t dev, void *softc)
{
	if (dev->softc && !(dev->flags & DF_EXTERNALSOFTC))
		free(dev->softc, M_BUS_SC);
	dev->softc = softc;
	if (dev->softc)
		dev->flags |= DF_EXTERNALSOFTC;
	else
		dev->flags &= ~DF_EXTERNALSOFTC;
}

/**
 * @brief Free claimed softc
 *
 * Most drivers do not need to use this since the softc is freed
 * automatically when the driver is detached.
 */
void
device_free_softc(void *softc)
{
	free(softc, M_BUS_SC);
}

/**
 * @brief Claim softc
 *
 * This function can be used to let the driver free the automatically
 * allocated softc using "device_free_softc()". This function is
 * useful when the driver is refcounting the softc and the softc
 * cannot be freed when the "device_detach" method is called.
 */
void
device_claim_softc(device_t dev)
{
	if (dev->softc)
		dev->flags |= DF_EXTERNALSOFTC;
	else
		dev->flags &= ~DF_EXTERNALSOFTC;
}

/**
 * @brief Get the device's ivars field
 *
 * The ivars field is used by the parent device to store per-device
 * state (e.g. the physical location of the device or a list of
 * resources).
 */
void *
device_get_ivars(device_t dev)
{

	KASSERT(dev != NULL, ("device_get_ivars(NULL, ...)"));
	return (dev->ivars);
}

/**
 * @brief Set the device's ivars field
 */
void
device_set_ivars(device_t dev, void * ivars)
{

	KASSERT(dev != NULL, ("device_set_ivars(NULL, ...)"));
	dev->ivars = ivars;
}

/**
 * @brief Return the device's state
 */
device_state_t
device_get_state(device_t dev)
{
	return (dev->state);
}

/**
 * @brief Set the DF_ENABLED flag for the device
 */
void
device_enable(device_t dev)
{
	dev->flags |= DF_ENABLED;
}

/**
 * @brief Clear the DF_ENABLED flag for the device
 */
void
device_disable(device_t dev)
{
	dev->flags &= ~DF_ENABLED;
}

/**
 * @brief Increment the busy counter for the device
 */
void
device_busy(device_t dev)
{
	if (dev->state < DS_ATTACHING)
		panic("device_busy: called for unattached device");
	if (dev->busy == 0 && dev->parent)
		device_busy(dev->parent);
	dev->busy++;
	if (dev->state == DS_ATTACHED)
		dev->state = DS_BUSY;
}

/**
 * @brief Decrement the busy counter for the device
 */
void
device_unbusy(device_t dev)
{
	if (dev->busy != 0 && dev->state != DS_BUSY &&
	    dev->state != DS_ATTACHING)
		panic("device_unbusy: called for non-busy device %s",
		    device_get_nameunit(dev));
	dev->busy--;
	if (dev->busy == 0) {
		if (dev->parent)
			device_unbusy(dev->parent);
		if (dev->state == DS_BUSY)
			dev->state = DS_ATTACHED;
	}
}

/**
 * @brief Set the DF_QUIET flag for the device
 */
void
device_quiet(device_t dev)
{
	dev->flags |= DF_QUIET;
}

/**
 * @brief Set the DF_QUIET_CHILDREN flag for the device
 */
void
device_quiet_children(device_t dev)
{
	dev->flags |= DF_QUIET_CHILDREN;
}

/**
 * @brief Clear the DF_QUIET flag for the device
 */
void
device_verbose(device_t dev)
{
	dev->flags &= ~DF_QUIET;
}

/**
 * @brief Return non-zero if the DF_QUIET_CHIDLREN flag is set on the device
 */
int
device_has_quiet_children(device_t dev)
{
	return ((dev->flags & DF_QUIET_CHILDREN) != 0);
}

/**
 * @brief Return non-zero if the DF_QUIET flag is set on the device
 */
int
device_is_quiet(device_t dev)
{
	return ((dev->flags & DF_QUIET) != 0);
}

/**
 * @brief Return non-zero if the DF_ENABLED flag is set on the device
 */
int
device_is_enabled(device_t dev)
{
	return ((dev->flags & DF_ENABLED) != 0);
}

/**
 * @brief Return non-zero if the device was successfully probed
 */
int
device_is_alive(device_t dev)
{
	return (dev->state >= DS_ALIVE);
}

/**
 * @brief Return non-zero if the device currently has a driver
 * attached to it
 */
int
device_is_attached(device_t dev)
{
	return (dev->state >= DS_ATTACHED);
}

/**
 * @brief Return non-zero if the device is currently suspended.
 */
int
device_is_suspended(device_t dev)
{
	return ((dev->flags & DF_SUSPENDED) != 0);
}

/**
 * @brief Set the devclass of a device
 * @see devclass_add_device().
 */
int
device_set_devclass(device_t dev, const char *classname)
{
	devclass_t dc;
	int error;

	if (!classname) {
		if (dev->devclass)
			devclass_delete_device(dev->devclass, dev);
		return (0);
	}

	if (dev->devclass) {
		printf("device_set_devclass: device class already set\n");
		return (EINVAL);
	}

	dc = devclass_find_internal(classname, NULL, TRUE);
	if (!dc)
		return (ENOMEM);

	error = devclass_add_device(dc, dev);

	bus_data_generation_update();
	return (error);
}

/**
 * @brief Set the devclass of a device and mark the devclass fixed.
 * @see device_set_devclass()
 */
int
device_set_devclass_fixed(device_t dev, const char *classname)
{
	int error;

	if (classname == NULL)
		return (EINVAL);

	error = device_set_devclass(dev, classname);
	if (error)
		return (error);
	dev->flags |= DF_FIXEDCLASS;
	return (0);
}

/**
 * @brief Query the device to determine if it's of a fixed devclass
 * @see device_set_devclass_fixed()
 */
bool
device_is_devclass_fixed(device_t dev)
{
	return ((dev->flags & DF_FIXEDCLASS) != 0);
}

/**
 * @brief Set the driver of a device
 *
 * @retval 0		success
 * @retval EBUSY	the device already has a driver attached
 * @retval ENOMEM	a memory allocation failure occurred
 */
int
device_set_driver(device_t dev, driver_t *driver)
{
	if (dev->state >= DS_ATTACHED)
		return (EBUSY);

	if (dev->driver == driver)
		return (0);

	if (dev->softc && !(dev->flags & DF_EXTERNALSOFTC)) {
		free(dev->softc, M_BUS_SC);
		dev->softc = NULL;
	}
	device_set_desc(dev, NULL);
	kobj_delete((kobj_t) dev, NULL);
	dev->driver = driver;
	if (driver) {
		kobj_init((kobj_t) dev, (kobj_class_t) driver);
		if (!(dev->flags & DF_EXTERNALSOFTC) && driver->size > 0) {
			dev->softc = malloc(driver->size, M_BUS_SC,
			    M_NOWAIT | M_ZERO);
			if (!dev->softc) {
				kobj_delete((kobj_t) dev, NULL);
				kobj_init((kobj_t) dev, &null_class);
				dev->driver = NULL;
				return (ENOMEM);
			}
		}
	} else {
		kobj_init((kobj_t) dev, &null_class);
	}

	bus_data_generation_update();
	return (0);
}

/**
 * @brief Probe a device, and return this status.
 *
 * This function is the core of the device autoconfiguration
 * system. Its purpose is to select a suitable driver for a device and
 * then call that driver to initialise the hardware appropriately. The
 * driver is selected by calling the DEVICE_PROBE() method of a set of
 * candidate drivers and then choosing the driver which returned the
 * best value. This driver is then attached to the device using
 * device_attach().
 *
 * The set of suitable drivers is taken from the list of drivers in
 * the parent device's devclass. If the device was originally created
 * with a specific class name (see device_add_child()), only drivers
 * with that name are probed, otherwise all drivers in the devclass
 * are probed. If no drivers return successful probe values in the
 * parent devclass, the search continues in the parent of that
 * devclass (see devclass_get_parent()) if any.
 *
 * @param dev		the device to initialise
 *
 * @retval 0		success
 * @retval ENXIO	no driver was found
 * @retval ENOMEM	memory allocation failure
 * @retval non-zero	some other unix error code
 * @retval -1		Device already attached
 */
int
device_probe(device_t dev)
{
	int error;

	GIANT_REQUIRED;

	if (dev->state >= DS_ALIVE && (dev->flags & DF_REBID) == 0)
		return (-1);

	if (!(dev->flags & DF_ENABLED)) {
		if (bootverbose && device_get_name(dev) != NULL) {
			device_print_prettyname(dev);
			printf("not probed (disabled)\n");
		}
		return (-1);
	}
	if ((error = device_probe_child(dev->parent, dev)) != 0) {
		if (bus_current_pass == BUS_PASS_DEFAULT &&
		    !(dev->flags & DF_DONENOMATCH)) {
			BUS_PROBE_NOMATCH(dev->parent, dev);
			devnomatch(dev);
			dev->flags |= DF_DONENOMATCH;
		}
		return (error);
	}
	return (0);
}

/**
 * @brief Probe a device and attach a driver if possible
 *
 * calls device_probe() and attaches if that was successful.
 */
int
device_probe_and_attach(device_t dev)
{
	int error;

	GIANT_REQUIRED;

	error = device_probe(dev);
	if (error == -1)
		return (0);
	else if (error != 0)
		return (error);

	CURVNET_SET_QUIET(vnet0);
	error = device_attach(dev);
	CURVNET_RESTORE();
	return error;
}

/**
 * @brief Attach a device driver to a device
 *
 * This function is a wrapper around the DEVICE_ATTACH() driver
 * method. In addition to calling DEVICE_ATTACH(), it initialises the
 * device's sysctl tree, optionally prints a description of the device
 * and queues a notification event for user-based device management
 * services.
 *
 * Normally this function is only called internally from
 * device_probe_and_attach().
 *
 * @param dev		the device to initialise
 *
 * @retval 0		success
 * @retval ENXIO	no driver was found
 * @retval ENOMEM	memory allocation failure
 * @retval non-zero	some other unix error code
 */
int
device_attach(device_t dev)
{
	uint64_t attachtime;
	uint16_t attachentropy;
	int error;

	if (resource_disabled(dev->driver->name, dev->unit)) {
		device_disable(dev);
		if (bootverbose)
			 device_printf(dev, "disabled via hints entry\n");
		return (ENXIO);
	}

	device_sysctl_init(dev);
	if (!device_is_quiet(dev))
		device_print_child(dev->parent, dev);
	attachtime = get_cyclecount();
	dev->state = DS_ATTACHING;
	if ((error = DEVICE_ATTACH(dev)) != 0) {
		printf("device_attach: %s%d attach returned %d\n",
		    dev->driver->name, dev->unit, error);
		if (!(dev->flags & DF_FIXEDCLASS))
			devclass_delete_device(dev->devclass, dev);
		(void)device_set_driver(dev, NULL);
		device_sysctl_fini(dev);
		KASSERT(dev->busy == 0, ("attach failed but busy"));
		dev->state = DS_NOTPRESENT;
		return (error);
	}
	dev->flags |= DF_ATTACHED_ONCE;
	/* We only need the low bits of this time, but ranges from tens to thousands
	 * have been seen, so keep 2 bytes' worth.
	 */
	attachentropy = (uint16_t)(get_cyclecount() - attachtime);
	random_harvest_direct(&attachentropy, sizeof(attachentropy), RANDOM_ATTACH);
	device_sysctl_update(dev);
	if (dev->busy)
		dev->state = DS_BUSY;
	else
		dev->state = DS_ATTACHED;
	dev->flags &= ~DF_DONENOMATCH;
	EVENTHANDLER_DIRECT_INVOKE(device_attach, dev);
	devadded(dev);
	return (0);
}

/**
 * @brief Detach a driver from a device
 *
 * This function is a wrapper around the DEVICE_DETACH() driver
 * method. If the call to DEVICE_DETACH() succeeds, it calls
 * BUS_CHILD_DETACHED() for the parent of @p dev, queues a
 * notification event for user-based device management services and
 * cleans up the device's sysctl tree.
 *
 * @param dev		the device to un-initialise
 *
 * @retval 0		success
 * @retval ENXIO	no driver was found
 * @retval ENOMEM	memory allocation failure
 * @retval non-zero	some other unix error code
 */
int
device_detach(device_t dev)
{
	int error;

	GIANT_REQUIRED;

	PDEBUG(("%s", DEVICENAME(dev)));
	if (dev->state == DS_BUSY)
		return (EBUSY);
	if (dev->state == DS_ATTACHING) {
		device_printf(dev, "device in attaching state! Deferring detach.\n");
		return (EBUSY);
	}
	if (dev->state != DS_ATTACHED)
		return (0);

	EVENTHANDLER_DIRECT_INVOKE(device_detach, dev, EVHDEV_DETACH_BEGIN);
	if ((error = DEVICE_DETACH(dev)) != 0) {
		EVENTHANDLER_DIRECT_INVOKE(device_detach, dev,
		    EVHDEV_DETACH_FAILED);
		return (error);
	} else {
		EVENTHANDLER_DIRECT_INVOKE(device_detach, dev,
		    EVHDEV_DETACH_COMPLETE);
	}
	devremoved(dev);
	if (!device_is_quiet(dev))
		device_printf(dev, "detached\n");
	if (dev->parent)
		BUS_CHILD_DETACHED(dev->parent, dev);

	if (!(dev->flags & DF_FIXEDCLASS))
		devclass_delete_device(dev->devclass, dev);

	device_verbose(dev);
	dev->state = DS_NOTPRESENT;
	(void)device_set_driver(dev, NULL);
	device_sysctl_fini(dev);

	return (0);
}

/**
 * @brief Tells a driver to quiesce itself.
 *
 * This function is a wrapper around the DEVICE_QUIESCE() driver
 * method. If the call to DEVICE_QUIESCE() succeeds.
 *
 * @param dev		the device to quiesce
 *
 * @retval 0		success
 * @retval ENXIO	no driver was found
 * @retval ENOMEM	memory allocation failure
 * @retval non-zero	some other unix error code
 */
int
device_quiesce(device_t dev)
{

	PDEBUG(("%s", DEVICENAME(dev)));
	if (dev->state == DS_BUSY)
		return (EBUSY);
	if (dev->state != DS_ATTACHED)
		return (0);

	return (DEVICE_QUIESCE(dev));
}

/**
 * @brief Notify a device of system shutdown
 *
 * This function calls the DEVICE_SHUTDOWN() driver method if the
 * device currently has an attached driver.
 *
 * @returns the value returned by DEVICE_SHUTDOWN()
 */
int
device_shutdown(device_t dev)
{
	if (dev->state < DS_ATTACHED)
		return (0);
	return (DEVICE_SHUTDOWN(dev));
}

/**
 * @brief Set the unit number of a device
 *
 * This function can be used to override the unit number used for a
 * device (e.g. to wire a device to a pre-configured unit number).
 */
int
device_set_unit(device_t dev, int unit)
{
	devclass_t dc;
	int err;

	dc = device_get_devclass(dev);
	if (unit < dc->maxunit && dc->devices[unit])
		return (EBUSY);
	err = devclass_delete_device(dc, dev);
	if (err)
		return (err);
	dev->unit = unit;
	err = devclass_add_device(dc, dev);
	if (err)
		return (err);

	bus_data_generation_update();
	return (0);
}

/*======================================*/
/*
 * Some useful method implementations to make life easier for bus drivers.
 */

void
resource_init_map_request_impl(struct resource_map_request *args, size_t sz)
{

	bzero(args, sz);
	args->size = sz;
	args->memattr = VM_MEMATTR_UNCACHEABLE;
}

/**
 * @brief Initialise a resource list.
 *
 * @param rl		the resource list to initialise
 */
void
resource_list_init(struct resource_list *rl)
{
	STAILQ_INIT(rl);
}

/**
 * @brief Reclaim memory used by a resource list.
 *
 * This function frees the memory for all resource entries on the list
 * (if any).
 *
 * @param rl		the resource list to free
 */
void
resource_list_free(struct resource_list *rl)
{
	struct resource_list_entry *rle;

	while ((rle = STAILQ_FIRST(rl)) != NULL) {
		if (rle->res)
			panic("resource_list_free: resource entry is busy");
		STAILQ_REMOVE_HEAD(rl, link);
		free(rle, M_BUS);
	}
}

/**
 * @brief Add a resource entry.
 *
 * This function adds a resource entry using the given @p type, @p
 * start, @p end and @p count values. A rid value is chosen by
 * searching sequentially for the first unused rid starting at zero.
 *
 * @param rl		the resource list to edit
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param start		the start address of the resource
 * @param end		the end address of the resource
 * @param count		XXX end-start+1
 */
int
resource_list_add_next(struct resource_list *rl, int type, rman_res_t start,
    rman_res_t end, rman_res_t count)
{
	int rid;

	rid = 0;
	while (resource_list_find(rl, type, rid) != NULL)
		rid++;
	resource_list_add(rl, type, rid, start, end, count);
	return (rid);
}

/**
 * @brief Add or modify a resource entry.
 *
 * If an existing entry exists with the same type and rid, it will be
 * modified using the given values of @p start, @p end and @p
 * count. If no entry exists, a new one will be created using the
 * given values.  The resource list entry that matches is then returned.
 *
 * @param rl		the resource list to edit
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param rid		the resource identifier
 * @param start		the start address of the resource
 * @param end		the end address of the resource
 * @param count		XXX end-start+1
 */
struct resource_list_entry *
resource_list_add(struct resource_list *rl, int type, int rid,
    rman_res_t start, rman_res_t end, rman_res_t count)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(rl, type, rid);
	if (!rle) {
		rle = malloc(sizeof(struct resource_list_entry), M_BUS,
		    M_NOWAIT);
		if (!rle)
			panic("resource_list_add: can't record entry");
		STAILQ_INSERT_TAIL(rl, rle, link);
		rle->type = type;
		rle->rid = rid;
		rle->res = NULL;
		rle->flags = 0;
	}

	if (rle->res)
		panic("resource_list_add: resource entry is busy");

	rle->start = start;
	rle->end = end;
	rle->count = count;
	return (rle);
}

/**
 * @brief Determine if a resource entry is busy.
 *
 * Returns true if a resource entry is busy meaning that it has an
 * associated resource that is not an unallocated "reserved" resource.
 *
 * @param rl		the resource list to search
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param rid		the resource identifier
 *
 * @returns Non-zero if the entry is busy, zero otherwise.
 */
int
resource_list_busy(struct resource_list *rl, int type, int rid)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(rl, type, rid);
	if (rle == NULL || rle->res == NULL)
		return (0);
	if ((rle->flags & (RLE_RESERVED | RLE_ALLOCATED)) == RLE_RESERVED) {
		KASSERT(!(rman_get_flags(rle->res) & RF_ACTIVE),
		    ("reserved resource is active"));
		return (0);
	}
	return (1);
}

/**
 * @brief Determine if a resource entry is reserved.
 *
 * Returns true if a resource entry is reserved meaning that it has an
 * associated "reserved" resource.  The resource can either be
 * allocated or unallocated.
 *
 * @param rl		the resource list to search
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param rid		the resource identifier
 *
 * @returns Non-zero if the entry is reserved, zero otherwise.
 */
int
resource_list_reserved(struct resource_list *rl, int type, int rid)
{
	struct resource_list_entry *rle;

	rle = resource_list_find(rl, type, rid);
	if (rle != NULL && rle->flags & RLE_RESERVED)
		return (1);
	return (0);
}

/**
 * @brief Find a resource entry by type and rid.
 *
 * @param rl		the resource list to search
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param rid		the resource identifier
 *
 * @returns the resource entry pointer or NULL if there is no such
 * entry.
 */
struct resource_list_entry *
resource_list_find(struct resource_list *rl, int type, int rid)
{
	struct resource_list_entry *rle;

	STAILQ_FOREACH(rle, rl, link) {
		if (rle->type == type && rle->rid == rid)
			return (rle);
	}
	return (NULL);
}

/**
 * @brief Delete a resource entry.
 *
 * @param rl		the resource list to edit
 * @param type		the resource entry type (e.g. SYS_RES_MEMORY)
 * @param rid		the resource identifier
 */
void
resource_list_delete(struct resource_list *rl, int type, int rid)
{
	struct resource_list_entry *rle = resource_list_find(rl, type, rid);

	if (rle) {
		if (rle->res != NULL)
			panic("resource_list_delete: resource has not been released");
		STAILQ_REMOVE(rl, rle, resource_list_entry, link);
		free(rle, M_BUS);
	}
}

/**
 * @brief Allocate a reserved resource
 *
 * This can be used by buses to force the allocation of resources
 * that are always active in the system even if they are not allocated
 * by a driver (e.g. PCI BARs).  This function is usually called when
 * adding a new child to the bus.  The resource is allocated from the
 * parent bus when it is reserved.  The resource list entry is marked
 * with RLE_RESERVED to note that it is a reserved resource.
 *
 * Subsequent attempts to allocate the resource with
 * resource_list_alloc() will succeed the first time and will set
 * RLE_ALLOCATED to note that it has been allocated.  When a reserved
 * resource that has been allocated is released with
 * resource_list_release() the resource RLE_ALLOCATED is cleared, but
 * the actual resource remains allocated.  The resource can be released to
 * the parent bus by calling resource_list_unreserve().
 *
 * @param rl		the resource list to allocate from
 * @param bus		the parent device of @p child
 * @param child		the device for which the resource is being reserved
 * @param type		the type of resource to allocate
 * @param rid		a pointer to the resource identifier
 * @param start		hint at the start of the resource range - pass
 *			@c 0 for any start address
 * @param end		hint at the end of the resource range - pass
 *			@c ~0 for any end address
 * @param count		hint at the size of range required - pass @c 1
 *			for any size
 * @param flags		any extra flags to control the resource
 *			allocation - see @c RF_XXX flags in
 *			<sys/rman.h> for details
 *
 * @returns		the resource which was allocated or @c NULL if no
 *			resource could be allocated
 */
struct resource *
resource_list_reserve(struct resource_list *rl, device_t bus, device_t child,
    int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);
	struct resource *r;

	if (passthrough)
		panic(
    "resource_list_reserve() should only be called for direct children");
	if (flags & RF_ACTIVE)
		panic(
    "resource_list_reserve() should only reserve inactive resources");

	r = resource_list_alloc(rl, bus, child, type, rid, start, end, count,
	    flags);
	if (r != NULL) {
		rle = resource_list_find(rl, type, *rid);
		rle->flags |= RLE_RESERVED;
	}
	return (r);
}

/**
 * @brief Helper function for implementing BUS_ALLOC_RESOURCE()
 *
 * Implement BUS_ALLOC_RESOURCE() by looking up a resource from the list
 * and passing the allocation up to the parent of @p bus. This assumes
 * that the first entry of @c device_get_ivars(child) is a struct
 * resource_list. This also handles 'passthrough' allocations where a
 * child is a remote descendant of bus by passing the allocation up to
 * the parent of bus.
 *
 * Typically, a bus driver would store a list of child resources
 * somewhere in the child device's ivars (see device_get_ivars()) and
 * its implementation of BUS_ALLOC_RESOURCE() would find that list and
 * then call resource_list_alloc() to perform the allocation.
 *
 * @param rl		the resource list to allocate from
 * @param bus		the parent device of @p child
 * @param child		the device which is requesting an allocation
 * @param type		the type of resource to allocate
 * @param rid		a pointer to the resource identifier
 * @param start		hint at the start of the resource range - pass
 *			@c 0 for any start address
 * @param end		hint at the end of the resource range - pass
 *			@c ~0 for any end address
 * @param count		hint at the size of range required - pass @c 1
 *			for any size
 * @param flags		any extra flags to control the resource
 *			allocation - see @c RF_XXX flags in
 *			<sys/rman.h> for details
 *
 * @returns		the resource which was allocated or @c NULL if no
 *			resource could be allocated
 */
struct resource *
resource_list_alloc(struct resource_list *rl, device_t bus, device_t child,
    int type, int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);
	int isdefault = RMAN_IS_DEFAULT_RANGE(start, end);

	if (passthrough) {
		return (BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
		    type, rid, start, end, count, flags));
	}

	rle = resource_list_find(rl, type, *rid);

	if (!rle)
		return (NULL);		/* no resource of that type/rid */

	if (rle->res) {
		if (rle->flags & RLE_RESERVED) {
			if (rle->flags & RLE_ALLOCATED)
				return (NULL);
			if ((flags & RF_ACTIVE) &&
			    bus_activate_resource(child, type, *rid,
			    rle->res) != 0)
				return (NULL);
			rle->flags |= RLE_ALLOCATED;
			return (rle->res);
		}
		device_printf(bus,
		    "resource entry %#x type %d for child %s is busy\n", *rid,
		    type, device_get_nameunit(child));
		return (NULL);
	}

	if (isdefault) {
		start = rle->start;
		count = ulmax(count, rle->count);
		end = ulmax(rle->end, start + count - 1);
	}

	rle->res = BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
	    type, rid, start, end, count, flags);

	/*
	 * Record the new range.
	 */
	if (rle->res) {
		rle->start = rman_get_start(rle->res);
		rle->end = rman_get_end(rle->res);
		rle->count = count;
	}

	return (rle->res);
}

/**
 * @brief Helper function for implementing BUS_RELEASE_RESOURCE()
 *
 * Implement BUS_RELEASE_RESOURCE() using a resource list. Normally
 * used with resource_list_alloc().
 *
 * @param rl		the resource list which was allocated from
 * @param bus		the parent device of @p child
 * @param child		the device which is requesting a release
 * @param type		the type of resource to release
 * @param rid		the resource identifier
 * @param res		the resource to release
 *
 * @retval 0		success
 * @retval non-zero	a standard unix error code indicating what
 *			error condition prevented the operation
 */
int
resource_list_release(struct resource_list *rl, device_t bus, device_t child,
    int type, int rid, struct resource *res)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);
	int error;

	if (passthrough) {
		return (BUS_RELEASE_RESOURCE(device_get_parent(bus), child,
		    type, rid, res));
	}

	rle = resource_list_find(rl, type, rid);

	if (!rle)
		panic("resource_list_release: can't find resource");
	if (!rle->res)
		panic("resource_list_release: resource entry is not busy");
	if (rle->flags & RLE_RESERVED) {
		if (rle->flags & RLE_ALLOCATED) {
			if (rman_get_flags(res) & RF_ACTIVE) {
				error = bus_deactivate_resource(child, type,
				    rid, res);
				if (error)
					return (error);
			}
			rle->flags &= ~RLE_ALLOCATED;
			return (0);
		}
		return (EINVAL);
	}

	error = BUS_RELEASE_RESOURCE(device_get_parent(bus), child,
	    type, rid, res);
	if (error)
		return (error);

	rle->res = NULL;
	return (0);
}

/**
 * @brief Release all active resources of a given type
 *
 * Release all active resources of a specified type.  This is intended
 * to be used to cleanup resources leaked by a driver after detach or
 * a failed attach.
 *
 * @param rl		the resource list which was allocated from
 * @param bus		the parent device of @p child
 * @param child		the device whose active resources are being released
 * @param type		the type of resources to release
 *
 * @retval 0		success
 * @retval EBUSY	at least one resource was active
 */
int
resource_list_release_active(struct resource_list *rl, device_t bus,
    device_t child, int type)
{
	struct resource_list_entry *rle;
	int error, retval;

	retval = 0;
	STAILQ_FOREACH(rle, rl, link) {
		if (rle->type != type)
			continue;
		if (rle->res == NULL)
			continue;
		if ((rle->flags & (RLE_RESERVED | RLE_ALLOCATED)) ==
		    RLE_RESERVED)
			continue;
		retval = EBUSY;
		error = resource_list_release(rl, bus, child, type,
		    rman_get_rid(rle->res), rle->res);
		if (error != 0)
			device_printf(bus,
			    "Failed to release active resource: %d\n", error);
	}
	return (retval);
}


/**
 * @brief Fully release a reserved resource
 *
 * Fully releases a resource reserved via resource_list_reserve().
 *
 * @param rl		the resource list which was allocated from
 * @param bus		the parent device of @p child
 * @param child		the device whose reserved resource is being released
 * @param type		the type of resource to release
 * @param rid		the resource identifier
 * @param res		the resource to release
 *
 * @retval 0		success
 * @retval non-zero	a standard unix error code indicating what
 *			error condition prevented the operation
 */
int
resource_list_unreserve(struct resource_list *rl, device_t bus, device_t child,
    int type, int rid)
{
	struct resource_list_entry *rle = NULL;
	int passthrough = (device_get_parent(child) != bus);

	if (passthrough)
		panic(
    "resource_list_unreserve() should only be called for direct children");

	rle = resource_list_find(rl, type, rid);

	if (!rle)
		panic("resource_list_unreserve: can't find resource");
	if (!(rle->flags & RLE_RESERVED))
		return (EINVAL);
	if (rle->flags & RLE_ALLOCATED)
		return (EBUSY);
	rle->flags &= ~RLE_RESERVED;
	return (resource_list_release(rl, bus, child, type, rid, rle->res));
}

/**
 * @brief Print a description of resources in a resource list
 *
 * Print all resources of a specified type, for use in BUS_PRINT_CHILD().
 * The name is printed if at least one resource of the given type is available.
 * The format is used to print resource start and end.
 *
 * @param rl		the resource list to print
 * @param name		the name of @p type, e.g. @c "memory"
 * @param type		type type of resource entry to print
 * @param format	printf(9) format string to print resource
 *			start and end values
 *
 * @returns		the number of characters printed
 */
int
resource_list_print_type(struct resource_list *rl, const char *name, int type,
    const char *format)
{
	struct resource_list_entry *rle;
	int printed, retval;

	printed = 0;
	retval = 0;
	/* Yes, this is kinda cheating */
	STAILQ_FOREACH(rle, rl, link) {
		if (rle->type == type) {
			if (printed == 0)
				retval += printf(" %s ", name);
			else
				retval += printf(",");
			printed++;
			retval += printf(format, rle->start);
			if (rle->count > 1) {
				retval += printf("-");
				retval += printf(format, rle->start +
						 rle->count - 1);
			}
		}
	}
	return (retval);
}

/**
 * @brief Releases all the resources in a list.
 *
 * @param rl		The resource list to purge.
 *
 * @returns		nothing
 */
void
resource_list_purge(struct resource_list *rl)
{
	struct resource_list_entry *rle;

	while ((rle = STAILQ_FIRST(rl)) != NULL) {
		if (rle->res)
			bus_release_resource(rman_get_device(rle->res),
			    rle->type, rle->rid, rle->res);
		STAILQ_REMOVE_HEAD(rl, link);
		free(rle, M_BUS);
	}
}

device_t
bus_generic_add_child(device_t dev, u_int order, const char *name, int unit)
{

	return (device_add_child_ordered(dev, order, name, unit));
}

/**
 * @brief Helper function for implementing DEVICE_PROBE()
 *
 * This function can be used to help implement the DEVICE_PROBE() for
 * a bus (i.e. a device which has other devices attached to it). It
 * calls the DEVICE_IDENTIFY() method of each driver in the device's
 * devclass.
 */
int
bus_generic_probe(device_t dev)
{
	devclass_t dc = dev->devclass;
	driverlink_t dl;

	TAILQ_FOREACH(dl, &dc->drivers, link) {
		/*
		 * If this driver's pass is too high, then ignore it.
		 * For most drivers in the default pass, this will
		 * never be true.  For early-pass drivers they will
		 * only call the identify routines of eligible drivers
		 * when this routine is called.  Drivers for later
		 * passes should have their identify routines called
		 * on early-pass buses during BUS_NEW_PASS().
		 */
		if (dl->pass > bus_current_pass)
			continue;
		DEVICE_IDENTIFY(dl->driver, dev);
	}

	return (0);
}

/**
 * @brief Helper function for implementing DEVICE_ATTACH()
 *
 * This function can be used to help implement the DEVICE_ATTACH() for
 * a bus. It calls device_probe_and_attach() for each of the device's
 * children.
 */
int
bus_generic_attach(device_t dev)
{
	device_t child;

	TAILQ_FOREACH(child, &dev->children, link) {
		device_probe_and_attach(child);
	}

	return (0);
}

/**
 * @brief Helper function for implementing DEVICE_DETACH()
 *
 * This function can be used to help implement the DEVICE_DETACH() for
 * a bus. It calls device_detach() for each of the device's
 * children.
 */
int
bus_generic_detach(device_t dev)
{
	device_t child;
	int error;

	if (dev->state != DS_ATTACHED)
		return (EBUSY);

	/*
	 * Detach children in the reverse order.
	 * See bus_generic_suspend for details.
	 */
	TAILQ_FOREACH_REVERSE(child, &dev->children, device_list, link) {
		if ((error = device_detach(child)) != 0)
			return (error);
	}

	return (0);
}

/**
 * @brief Helper function for implementing DEVICE_SHUTDOWN()
 *
 * This function can be used to help implement the DEVICE_SHUTDOWN()
 * for a bus. It calls device_shutdown() for each of the device's
 * children.
 */
int
bus_generic_shutdown(device_t dev)
{
	device_t child;

	/*
	 * Shut down children in the reverse order.
	 * See bus_generic_suspend for details.
	 */
	TAILQ_FOREACH_REVERSE(child, &dev->children, device_list, link) {
		device_shutdown(child);
	}

	return (0);
}

/**
 * @brief Default function for suspending a child device.
 *
 * This function is to be used by a bus's DEVICE_SUSPEND_CHILD().
 */
int
bus_generic_suspend_child(device_t dev, device_t child)
{
	int	error;

	error = DEVICE_SUSPEND(child);

	if (error == 0)
		child->flags |= DF_SUSPENDED;

	return (error);
}

/**
 * @brief Default function for resuming a child device.
 *
 * This function is to be used by a bus's DEVICE_RESUME_CHILD().
 */
int
bus_generic_resume_child(device_t dev, device_t child)
{

	DEVICE_RESUME(child);
	child->flags &= ~DF_SUSPENDED;

	return (0);
}

/**
 * @brief Helper function for implementing DEVICE_SUSPEND()
 *
 * This function can be used to help implement the DEVICE_SUSPEND()
 * for a bus. It calls DEVICE_SUSPEND() for each of the device's
 * children. If any call to DEVICE_SUSPEND() fails, the suspend
 * operation is aborted and any devices which were suspended are
 * resumed immediately by calling their DEVICE_RESUME() methods.
 */
int
bus_generic_suspend(device_t dev)
{
	int		error;
	device_t	child;

	/*
	 * Suspend children in the reverse order.
	 * For most buses all children are equal, so the order does not matter.
	 * Other buses, such as acpi, carefully order their child devices to
	 * express implicit dependencies between them.  For such buses it is
	 * safer to bring down devices in the reverse order.
	 */
	TAILQ_FOREACH_REVERSE(child, &dev->children, device_list, link) {
		error = BUS_SUSPEND_CHILD(dev, child);
		if (error != 0) {
			child = TAILQ_NEXT(child, link);
			if (child != NULL) {
				TAILQ_FOREACH_FROM(child, &dev->children, link)
					BUS_RESUME_CHILD(dev, child);
			}
			return (error);
		}
	}
	return (0);
}

/**
 * @brief Helper function for implementing DEVICE_RESUME()
 *
 * This function can be used to help implement the DEVICE_RESUME() for
 * a bus. It calls DEVICE_RESUME() on each of the device's children.
 */
int
bus_generic_resume(device_t dev)
{
	device_t	child;

	TAILQ_FOREACH(child, &dev->children, link) {
		BUS_RESUME_CHILD(dev, child);
		/* if resume fails, there's nothing we can usefully do... */
	}
	return (0);
}

/**
 * @brief Helper function for implementing BUS_PRINT_CHILD().
 *
 * This function prints the first part of the ascii representation of
 * @p child, including its name, unit and description (if any - see
 * device_set_desc()).
 *
 * @returns the number of characters printed
 */
int
bus_print_child_header(device_t dev, device_t child)
{
	int	retval = 0;

	if (device_get_desc(child)) {
		retval += device_printf(child, "<%s>", device_get_desc(child));
	} else {
		retval += printf("%s", device_get_nameunit(child));
	}

	return (retval);
}

/**
 * @brief Helper function for implementing BUS_PRINT_CHILD().
 *
 * This function prints the last part of the ascii representation of
 * @p child, which consists of the string @c " on " followed by the
 * name and unit of the @p dev.
 *
 * @returns the number of characters printed
 */
int
bus_print_child_footer(device_t dev, device_t child)
{
	return (printf(" on %s\n", device_get_nameunit(dev)));
}

/**
 * @brief Helper function for implementing BUS_PRINT_CHILD().
 *
 * This function prints out the VM domain for the given device.
 *
 * @returns the number of characters printed
 */
int
bus_print_child_domain(device_t dev, device_t child)
{
	int domain;

	/* No domain? Don't print anything */
	if (BUS_GET_DOMAIN(dev, child, &domain) != 0)
		return (0);

	return (printf(" numa-domain %d", domain));
}

/**
 * @brief Helper function for implementing BUS_PRINT_CHILD().
 *
 * This function simply calls bus_print_child_header() followed by
 * bus_print_child_footer().
 *
 * @returns the number of characters printed
 */
int
bus_generic_print_child(device_t dev, device_t child)
{
	int	retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += bus_print_child_domain(dev, child);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

/**
 * @brief Stub function for implementing BUS_READ_IVAR().
 *
 * @returns ENOENT
 */
int
bus_generic_read_ivar(device_t dev, device_t child, int index,
    uintptr_t * result)
{
	return (ENOENT);
}

/**
 * @brief Stub function for implementing BUS_WRITE_IVAR().
 *
 * @returns ENOENT
 */
int
bus_generic_write_ivar(device_t dev, device_t child, int index,
    uintptr_t value)
{
	return (ENOENT);
}

/**
 * @brief Stub function for implementing BUS_GET_RESOURCE_LIST().
 *
 * @returns NULL
 */
struct resource_list *
bus_generic_get_resource_list(device_t dev, device_t child)
{
	return (NULL);
}

/**
 * @brief Helper function for implementing BUS_DRIVER_ADDED().
 *
 * This implementation of BUS_DRIVER_ADDED() simply calls the driver's
 * DEVICE_IDENTIFY() method to allow it to add new children to the bus
 * and then calls device_probe_and_attach() for each unattached child.
 */
void
bus_generic_driver_added(device_t dev, driver_t *driver)
{
	device_t child;

	DEVICE_IDENTIFY(driver, dev);
	TAILQ_FOREACH(child, &dev->children, link) {
		if (child->state == DS_NOTPRESENT ||
		    (child->flags & DF_REBID))
			device_probe_and_attach(child);
	}
}

/**
 * @brief Helper function for implementing BUS_NEW_PASS().
 *
 * This implementing of BUS_NEW_PASS() first calls the identify
 * routines for any drivers that probe at the current pass.  Then it
 * walks the list of devices for this bus.  If a device is already
 * attached, then it calls BUS_NEW_PASS() on that device.  If the
 * device is not already attached, it attempts to attach a driver to
 * it.
 */
void
bus_generic_new_pass(device_t dev)
{
	driverlink_t dl;
	devclass_t dc;
	device_t child;

	dc = dev->devclass;
	TAILQ_FOREACH(dl, &dc->drivers, link) {
		if (dl->pass == bus_current_pass)
			DEVICE_IDENTIFY(dl->driver, dev);
	}
	TAILQ_FOREACH(child, &dev->children, link) {
		if (child->state >= DS_ATTACHED)
			BUS_NEW_PASS(child);
		else if (child->state == DS_NOTPRESENT)
			device_probe_and_attach(child);
	}
}

/**
 * @brief Helper function for implementing BUS_SETUP_INTR().
 *
 * This simple implementation of BUS_SETUP_INTR() simply calls the
 * BUS_SETUP_INTR() method of the parent of @p dev.
 */
int
bus_generic_setup_intr(device_t dev, device_t child, struct resource *irq,
    int flags, driver_filter_t *filter, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_SETUP_INTR(dev->parent, child, irq, flags,
		    filter, intr, arg, cookiep));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_TEARDOWN_INTR().
 *
 * This simple implementation of BUS_TEARDOWN_INTR() simply calls the
 * BUS_TEARDOWN_INTR() method of the parent of @p dev.
 */
int
bus_generic_teardown_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_TEARDOWN_INTR(dev->parent, child, irq, cookie));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_SUSPEND_INTR().
 *
 * This simple implementation of BUS_SUSPEND_INTR() simply calls the
 * BUS_SUSPEND_INTR() method of the parent of @p dev.
 */
int
bus_generic_suspend_intr(device_t dev, device_t child, struct resource *irq)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_SUSPEND_INTR(dev->parent, child, irq));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_RESUME_INTR().
 *
 * This simple implementation of BUS_RESUME_INTR() simply calls the
 * BUS_RESUME_INTR() method of the parent of @p dev.
 */
int
bus_generic_resume_intr(device_t dev, device_t child, struct resource *irq)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_RESUME_INTR(dev->parent, child, irq));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_ADJUST_RESOURCE().
 *
 * This simple implementation of BUS_ADJUST_RESOURCE() simply calls the
 * BUS_ADJUST_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_adjust_resource(device_t dev, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_ADJUST_RESOURCE(dev->parent, child, type, r, start,
		    end));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_ALLOC_RESOURCE().
 *
 * This simple implementation of BUS_ALLOC_RESOURCE() simply calls the
 * BUS_ALLOC_RESOURCE() method of the parent of @p dev.
 */
struct resource *
bus_generic_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_ALLOC_RESOURCE(dev->parent, child, type, rid,
		    start, end, count, flags));
	return (NULL);
}

/**
 * @brief Helper function for implementing BUS_RELEASE_RESOURCE().
 *
 * This simple implementation of BUS_RELEASE_RESOURCE() simply calls the
 * BUS_RELEASE_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_RELEASE_RESOURCE(dev->parent, child, type, rid,
		    r));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_ACTIVATE_RESOURCE().
 *
 * This simple implementation of BUS_ACTIVATE_RESOURCE() simply calls the
 * BUS_ACTIVATE_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_activate_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_ACTIVATE_RESOURCE(dev->parent, child, type, rid,
		    r));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_DEACTIVATE_RESOURCE().
 *
 * This simple implementation of BUS_DEACTIVATE_RESOURCE() simply calls the
 * BUS_DEACTIVATE_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_deactivate_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_DEACTIVATE_RESOURCE(dev->parent, child, type, rid,
		    r));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_MAP_RESOURCE().
 *
 * This simple implementation of BUS_MAP_RESOURCE() simply calls the
 * BUS_MAP_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_map_resource(device_t dev, device_t child, int type,
    struct resource *r, struct resource_map_request *args,
    struct resource_map *map)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_MAP_RESOURCE(dev->parent, child, type, r, args,
		    map));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_UNMAP_RESOURCE().
 *
 * This simple implementation of BUS_UNMAP_RESOURCE() simply calls the
 * BUS_UNMAP_RESOURCE() method of the parent of @p dev.
 */
int
bus_generic_unmap_resource(device_t dev, device_t child, int type,
    struct resource *r, struct resource_map *map)
{
	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_UNMAP_RESOURCE(dev->parent, child, type, r, map));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_BIND_INTR().
 *
 * This simple implementation of BUS_BIND_INTR() simply calls the
 * BUS_BIND_INTR() method of the parent of @p dev.
 */
int
bus_generic_bind_intr(device_t dev, device_t child, struct resource *irq,
    int cpu)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_BIND_INTR(dev->parent, child, irq, cpu));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_CONFIG_INTR().
 *
 * This simple implementation of BUS_CONFIG_INTR() simply calls the
 * BUS_CONFIG_INTR() method of the parent of @p dev.
 */
int
bus_generic_config_intr(device_t dev, int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_CONFIG_INTR(dev->parent, irq, trig, pol));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_DESCRIBE_INTR().
 *
 * This simple implementation of BUS_DESCRIBE_INTR() simply calls the
 * BUS_DESCRIBE_INTR() method of the parent of @p dev.
 */
int
bus_generic_describe_intr(device_t dev, device_t child, struct resource *irq,
    void *cookie, const char *descr)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent)
		return (BUS_DESCRIBE_INTR(dev->parent, child, irq, cookie,
		    descr));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_GET_CPUS().
 *
 * This simple implementation of BUS_GET_CPUS() simply calls the
 * BUS_GET_CPUS() method of the parent of @p dev.
 */
int
bus_generic_get_cpus(device_t dev, device_t child, enum cpu_sets op,
    size_t setsize, cpuset_t *cpuset)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent != NULL)
		return (BUS_GET_CPUS(dev->parent, child, op, setsize, cpuset));
	return (EINVAL);
}

/**
 * @brief Helper function for implementing BUS_GET_DMA_TAG().
 *
 * This simple implementation of BUS_GET_DMA_TAG() simply calls the
 * BUS_GET_DMA_TAG() method of the parent of @p dev.
 */
bus_dma_tag_t
bus_generic_get_dma_tag(device_t dev, device_t child)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent != NULL)
		return (BUS_GET_DMA_TAG(dev->parent, child));
	return (NULL);
}

/**
 * @brief Helper function for implementing BUS_GET_BUS_TAG().
 *
 * This simple implementation of BUS_GET_BUS_TAG() simply calls the
 * BUS_GET_BUS_TAG() method of the parent of @p dev.
 */
bus_space_tag_t
bus_generic_get_bus_tag(device_t dev, device_t child)
{

	/* Propagate up the bus hierarchy until someone handles it. */
	if (dev->parent != NULL)
		return (BUS_GET_BUS_TAG(dev->parent, child));
	return ((bus_space_tag_t)0);
}

/**
 * @brief Helper function for implementing BUS_GET_RESOURCE().
 *
 * This implementation of BUS_GET_RESOURCE() uses the
 * resource_list_find() function to do most of the work. It calls
 * BUS_GET_RESOURCE_LIST() to find a suitable resource list to
 * search.
 */
int
bus_generic_rl_get_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t *startp, rman_res_t *countp)
{
	struct resource_list *		rl = NULL;
	struct resource_list_entry *	rle = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return (EINVAL);

	rle = resource_list_find(rl, type, rid);
	if (!rle)
		return (ENOENT);

	if (startp)
		*startp = rle->start;
	if (countp)
		*countp = rle->count;

	return (0);
}

/**
 * @brief Helper function for implementing BUS_SET_RESOURCE().
 *
 * This implementation of BUS_SET_RESOURCE() uses the
 * resource_list_add() function to do most of the work. It calls
 * BUS_GET_RESOURCE_LIST() to find a suitable resource list to
 * edit.
 */
int
bus_generic_rl_set_resource(device_t dev, device_t child, int type, int rid,
    rman_res_t start, rman_res_t count)
{
	struct resource_list *		rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return (EINVAL);

	resource_list_add(rl, type, rid, start, (start + count - 1), count);

	return (0);
}

/**
 * @brief Helper function for implementing BUS_DELETE_RESOURCE().
 *
 * This implementation of BUS_DELETE_RESOURCE() uses the
 * resource_list_delete() function to do most of the work. It calls
 * BUS_GET_RESOURCE_LIST() to find a suitable resource list to
 * edit.
 */
void
bus_generic_rl_delete_resource(device_t dev, device_t child, int type, int rid)
{
	struct resource_list *		rl = NULL;

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return;

	resource_list_delete(rl, type, rid);

	return;
}

/**
 * @brief Helper function for implementing BUS_RELEASE_RESOURCE().
 *
 * This implementation of BUS_RELEASE_RESOURCE() uses the
 * resource_list_release() function to do most of the work. It calls
 * BUS_GET_RESOURCE_LIST() to find a suitable resource list.
 */
int
bus_generic_rl_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct resource_list *		rl = NULL;

	if (device_get_parent(child) != dev)
		return (BUS_RELEASE_RESOURCE(device_get_parent(dev), child,
		    type, rid, r));

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return (EINVAL);

	return (resource_list_release(rl, dev, child, type, rid, r));
}

/**
 * @brief Helper function for implementing BUS_ALLOC_RESOURCE().
 *
 * This implementation of BUS_ALLOC_RESOURCE() uses the
 * resource_list_alloc() function to do most of the work. It calls
 * BUS_GET_RESOURCE_LIST() to find a suitable resource list.
 */
struct resource *
bus_generic_rl_alloc_resource(device_t dev, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource_list *		rl = NULL;

	if (device_get_parent(child) != dev)
		return (BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
		    type, rid, start, end, count, flags));

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (!rl)
		return (NULL);

	return (resource_list_alloc(rl, dev, child, type, rid,
	    start, end, count, flags));
}

/**
 * @brief Helper function for implementing BUS_CHILD_PRESENT().
 *
 * This simple implementation of BUS_CHILD_PRESENT() simply calls the
 * BUS_CHILD_PRESENT() method of the parent of @p dev.
 */
int
bus_generic_child_present(device_t dev, device_t child)
{
	return (BUS_CHILD_PRESENT(device_get_parent(dev), dev));
}

int
bus_generic_get_domain(device_t dev, device_t child, int *domain)
{

	if (dev->parent)
		return (BUS_GET_DOMAIN(dev->parent, dev, domain));

	return (ENOENT);
}

/**
 * @brief Helper function for implementing BUS_RESCAN().
 *
 * This null implementation of BUS_RESCAN() always fails to indicate
 * the bus does not support rescanning.
 */
int
bus_null_rescan(device_t dev)
{

	return (ENXIO);
}

/*
 * Some convenience functions to make it easier for drivers to use the
 * resource-management functions.  All these really do is hide the
 * indirection through the parent's method table, making for slightly
 * less-wordy code.  In the future, it might make sense for this code
 * to maintain some sort of a list of resources allocated by each device.
 */

int
bus_alloc_resources(device_t dev, struct resource_spec *rs,
    struct resource **res)
{
	int i;

	for (i = 0; rs[i].type != -1; i++)
		res[i] = NULL;
	for (i = 0; rs[i].type != -1; i++) {
		res[i] = bus_alloc_resource_any(dev,
		    rs[i].type, &rs[i].rid, rs[i].flags);
		if (res[i] == NULL && !(rs[i].flags & RF_OPTIONAL)) {
			bus_release_resources(dev, rs, res);
			return (ENXIO);
		}
	}
	return (0);
}

void
bus_release_resources(device_t dev, const struct resource_spec *rs,
    struct resource **res)
{
	int i;

	for (i = 0; rs[i].type != -1; i++)
		if (res[i] != NULL) {
			bus_release_resource(
			    dev, rs[i].type, rs[i].rid, res[i]);
			res[i] = NULL;
		}
}

/**
 * @brief Wrapper function for BUS_ALLOC_RESOURCE().
 *
 * This function simply calls the BUS_ALLOC_RESOURCE() method of the
 * parent of @p dev.
 */
struct resource *
bus_alloc_resource(device_t dev, int type, int *rid, rman_res_t start,
    rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *res;

	if (dev->parent == NULL)
		return (NULL);
	res = BUS_ALLOC_RESOURCE(dev->parent, dev, type, rid, start, end,
	    count, flags);
	return (res);
}

/**
 * @brief Wrapper function for BUS_ADJUST_RESOURCE().
 *
 * This function simply calls the BUS_ADJUST_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_adjust_resource(device_t dev, int type, struct resource *r, rman_res_t start,
    rman_res_t end)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_ADJUST_RESOURCE(dev->parent, dev, type, r, start, end));
}

/**
 * @brief Wrapper function for BUS_ACTIVATE_RESOURCE().
 *
 * This function simply calls the BUS_ACTIVATE_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_activate_resource(device_t dev, int type, int rid, struct resource *r)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_ACTIVATE_RESOURCE(dev->parent, dev, type, rid, r));
}

/**
 * @brief Wrapper function for BUS_DEACTIVATE_RESOURCE().
 *
 * This function simply calls the BUS_DEACTIVATE_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_deactivate_resource(device_t dev, int type, int rid, struct resource *r)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_DEACTIVATE_RESOURCE(dev->parent, dev, type, rid, r));
}

/**
 * @brief Wrapper function for BUS_MAP_RESOURCE().
 *
 * This function simply calls the BUS_MAP_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_map_resource(device_t dev, int type, struct resource *r,
    struct resource_map_request *args, struct resource_map *map)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_MAP_RESOURCE(dev->parent, dev, type, r, args, map));
}

/**
 * @brief Wrapper function for BUS_UNMAP_RESOURCE().
 *
 * This function simply calls the BUS_UNMAP_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_unmap_resource(device_t dev, int type, struct resource *r,
    struct resource_map *map)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_UNMAP_RESOURCE(dev->parent, dev, type, r, map));
}

/**
 * @brief Wrapper function for BUS_RELEASE_RESOURCE().
 *
 * This function simply calls the BUS_RELEASE_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_release_resource(device_t dev, int type, int rid, struct resource *r)
{
	int rv;

	if (dev->parent == NULL)
		return (EINVAL);
	rv = BUS_RELEASE_RESOURCE(dev->parent, dev, type, rid, r);
	return (rv);
}

/**
 * @brief Wrapper function for BUS_SETUP_INTR().
 *
 * This function simply calls the BUS_SETUP_INTR() method of the
 * parent of @p dev.
 */
int
bus_setup_intr(device_t dev, struct resource *r, int flags,
    driver_filter_t filter, driver_intr_t handler, void *arg, void **cookiep)
{
	int error;

	if (dev->parent == NULL)
		return (EINVAL);
	error = BUS_SETUP_INTR(dev->parent, dev, r, flags, filter, handler,
	    arg, cookiep);
	if (error != 0)
		return (error);
	if (handler != NULL && !(flags & INTR_MPSAFE))
		device_printf(dev, "[GIANT-LOCKED]\n");
	return (0);
}

/**
 * @brief Wrapper function for BUS_TEARDOWN_INTR().
 *
 * This function simply calls the BUS_TEARDOWN_INTR() method of the
 * parent of @p dev.
 */
int
bus_teardown_intr(device_t dev, struct resource *r, void *cookie)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_TEARDOWN_INTR(dev->parent, dev, r, cookie));
}

/**
 * @brief Wrapper function for BUS_SUSPEND_INTR().
 *
 * This function simply calls the BUS_SUSPEND_INTR() method of the
 * parent of @p dev.
 */
int
bus_suspend_intr(device_t dev, struct resource *r)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_SUSPEND_INTR(dev->parent, dev, r));
}

/**
 * @brief Wrapper function for BUS_RESUME_INTR().
 *
 * This function simply calls the BUS_RESUME_INTR() method of the
 * parent of @p dev.
 */
int
bus_resume_intr(device_t dev, struct resource *r)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_RESUME_INTR(dev->parent, dev, r));
}

/**
 * @brief Wrapper function for BUS_BIND_INTR().
 *
 * This function simply calls the BUS_BIND_INTR() method of the
 * parent of @p dev.
 */
int
bus_bind_intr(device_t dev, struct resource *r, int cpu)
{
	if (dev->parent == NULL)
		return (EINVAL);
	return (BUS_BIND_INTR(dev->parent, dev, r, cpu));
}

/**
 * @brief Wrapper function for BUS_DESCRIBE_INTR().
 *
 * This function first formats the requested description into a
 * temporary buffer and then calls the BUS_DESCRIBE_INTR() method of
 * the parent of @p dev.
 */
int
bus_describe_intr(device_t dev, struct resource *irq, void *cookie,
    const char *fmt, ...)
{
	va_list ap;
	char descr[MAXCOMLEN + 1];

	if (dev->parent == NULL)
		return (EINVAL);
	va_start(ap, fmt);
	vsnprintf(descr, sizeof(descr), fmt, ap);
	va_end(ap);
	return (BUS_DESCRIBE_INTR(dev->parent, dev, irq, cookie, descr));
}

/**
 * @brief Wrapper function for BUS_SET_RESOURCE().
 *
 * This function simply calls the BUS_SET_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_set_resource(device_t dev, int type, int rid,
    rman_res_t start, rman_res_t count)
{
	return (BUS_SET_RESOURCE(device_get_parent(dev), dev, type, rid,
	    start, count));
}

/**
 * @brief Wrapper function for BUS_GET_RESOURCE().
 *
 * This function simply calls the BUS_GET_RESOURCE() method of the
 * parent of @p dev.
 */
int
bus_get_resource(device_t dev, int type, int rid,
    rman_res_t *startp, rman_res_t *countp)
{
	return (BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
	    startp, countp));
}

/**
 * @brief Wrapper function for BUS_GET_RESOURCE().
 *
 * This function simply calls the BUS_GET_RESOURCE() method of the
 * parent of @p dev and returns the start value.
 */
rman_res_t
bus_get_resource_start(device_t dev, int type, int rid)
{
	rman_res_t start;
	rman_res_t count;
	int error;

	error = BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
	    &start, &count);
	if (error)
		return (0);
	return (start);
}

/**
 * @brief Wrapper function for BUS_GET_RESOURCE().
 *
 * This function simply calls the BUS_GET_RESOURCE() method of the
 * parent of @p dev and returns the count value.
 */
rman_res_t
bus_get_resource_count(device_t dev, int type, int rid)
{
	rman_res_t start;
	rman_res_t count;
	int error;

	error = BUS_GET_RESOURCE(device_get_parent(dev), dev, type, rid,
	    &start, &count);
	if (error)
		return (0);
	return (count);
}

/**
 * @brief Wrapper function for BUS_DELETE_RESOURCE().
 *
 * This function simply calls the BUS_DELETE_RESOURCE() method of the
 * parent of @p dev.
 */
void
bus_delete_resource(device_t dev, int type, int rid)
{
	BUS_DELETE_RESOURCE(device_get_parent(dev), dev, type, rid);
}

/**
 * @brief Wrapper function for BUS_CHILD_PRESENT().
 *
 * This function simply calls the BUS_CHILD_PRESENT() method of the
 * parent of @p dev.
 */
int
bus_child_present(device_t child)
{
	return (BUS_CHILD_PRESENT(device_get_parent(child), child));
}

/**
 * @brief Wrapper function for BUS_CHILD_PNPINFO_STR().
 *
 * This function simply calls the BUS_CHILD_PNPINFO_STR() method of the
 * parent of @p dev.
 */
int
bus_child_pnpinfo_str(device_t child, char *buf, size_t buflen)
{
	device_t parent;

	parent = device_get_parent(child);
	if (parent == NULL) {
		*buf = '\0';
		return (0);
	}
	return (BUS_CHILD_PNPINFO_STR(parent, child, buf, buflen));
}

/**
 * @brief Wrapper function for BUS_CHILD_LOCATION_STR().
 *
 * This function simply calls the BUS_CHILD_LOCATION_STR() method of the
 * parent of @p dev.
 */
int
bus_child_location_str(device_t child, char *buf, size_t buflen)
{
	device_t parent;

	parent = device_get_parent(child);
	if (parent == NULL) {
		*buf = '\0';
		return (0);
	}
	return (BUS_CHILD_LOCATION_STR(parent, child, buf, buflen));
}

/**
 * @brief Wrapper function for BUS_GET_CPUS().
 *
 * This function simply calls the BUS_GET_CPUS() method of the
 * parent of @p dev.
 */
int
bus_get_cpus(device_t dev, enum cpu_sets op, size_t setsize, cpuset_t *cpuset)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return (EINVAL);
	return (BUS_GET_CPUS(parent, dev, op, setsize, cpuset));
}

/**
 * @brief Wrapper function for BUS_GET_DMA_TAG().
 *
 * This function simply calls the BUS_GET_DMA_TAG() method of the
 * parent of @p dev.
 */
bus_dma_tag_t
bus_get_dma_tag(device_t dev)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return (NULL);
	return (BUS_GET_DMA_TAG(parent, dev));
}

/**
 * @brief Wrapper function for BUS_GET_BUS_TAG().
 *
 * This function simply calls the BUS_GET_BUS_TAG() method of the
 * parent of @p dev.
 */
bus_space_tag_t
bus_get_bus_tag(device_t dev)
{
	device_t parent;

	parent = device_get_parent(dev);
	if (parent == NULL)
		return ((bus_space_tag_t)0);
	return (BUS_GET_BUS_TAG(parent, dev));
}

/**
 * @brief Wrapper function for BUS_GET_DOMAIN().
 *
 * This function simply calls the BUS_GET_DOMAIN() method of the
 * parent of @p dev.
 */
int
bus_get_domain(device_t dev, int *domain)
{
	return (BUS_GET_DOMAIN(device_get_parent(dev), dev, domain));
}

/* Resume all devices and then notify userland that we're up again. */
static int
root_resume(device_t dev)
{
	int error;

	error = bus_generic_resume(dev);
	if (error == 0)
		devctl_notify("kern", "power", "resume", NULL);
	return (error);
}

static int
root_print_child(device_t dev, device_t child)
{
	int	retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += printf("\n");

	return (retval);
}

static int
root_setup_intr(device_t dev, device_t child, struct resource *irq, int flags,
    driver_filter_t *filter, driver_intr_t *intr, void *arg, void **cookiep)
{
	/*
	 * If an interrupt mapping gets to here something bad has happened.
	 */
	panic("root_setup_intr");
}

/*
 * If we get here, assume that the device is permanent and really is
 * present in the system.  Removable bus drivers are expected to intercept
 * this call long before it gets here.  We return -1 so that drivers that
 * really care can check vs -1 or some ERRNO returned higher in the food
 * chain.
 */
static int
root_child_present(device_t dev, device_t child)
{
	return (-1);
}

static int
root_get_cpus(device_t dev, device_t child, enum cpu_sets op, size_t setsize,
    cpuset_t *cpuset)
{

	switch (op) {
	case INTR_CPUS:
		/* Default to returning the set of all CPUs. */
		if (setsize != sizeof(cpuset_t))
			return (EINVAL);
		*cpuset = all_cpus;
		return (0);
	default:
		return (EINVAL);
	}
}

static kobj_method_t root_methods[] = {
	/* Device interface */
	KOBJMETHOD(device_shutdown,	bus_generic_shutdown),
	KOBJMETHOD(device_suspend,	bus_generic_suspend),
	KOBJMETHOD(device_resume,	root_resume),

	/* Bus interface */
	KOBJMETHOD(bus_print_child,	root_print_child),
	KOBJMETHOD(bus_read_ivar,	bus_generic_read_ivar),
	KOBJMETHOD(bus_write_ivar,	bus_generic_write_ivar),
	KOBJMETHOD(bus_setup_intr,	root_setup_intr),
	KOBJMETHOD(bus_child_present,	root_child_present),
	KOBJMETHOD(bus_get_cpus,	root_get_cpus),

	KOBJMETHOD_END
};

static driver_t root_driver = {
	"root",
	root_methods,
	1,			/* no softc */
};

device_t	root_bus;
devclass_t	root_devclass;

static int
root_bus_module_handler(module_t mod, int what, void* arg)
{
	switch (what) {
	case MOD_LOAD:
		TAILQ_INIT(&bus_data_devices);
		kobj_class_compile((kobj_class_t) &root_driver);
		root_bus = make_device(NULL, "root", 0);
		root_bus->desc = "System root bus";
		kobj_init((kobj_t) root_bus, (kobj_class_t) &root_driver);
		root_bus->driver = &root_driver;
		root_bus->state = DS_ATTACHED;
		root_devclass = devclass_find_internal("root", NULL, FALSE);
		devinit();
		return (0);

	case MOD_SHUTDOWN:
		device_shutdown(root_bus);
		return (0);
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t root_bus_mod = {
	"rootbus",
	root_bus_module_handler,
	NULL
};
DECLARE_MODULE(rootbus, root_bus_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);

/**
 * @brief Automatically configure devices
 *
 * This function begins the autoconfiguration process by calling
 * device_probe_and_attach() for each child of the @c root0 device.
 */
void
root_bus_configure(void)
{

	PDEBUG(("."));

	/* Eventually this will be split up, but this is sufficient for now. */
	bus_set_pass(BUS_PASS_DEFAULT);
}

/**
 * @brief Module handler for registering device drivers
 *
 * This module handler is used to automatically register device
 * drivers when modules are loaded. If @p what is MOD_LOAD, it calls
 * devclass_add_driver() for the driver described by the
 * driver_module_data structure pointed to by @p arg
 */
int
driver_module_handler(module_t mod, int what, void *arg)
{
	struct driver_module_data *dmd;
	devclass_t bus_devclass;
	kobj_class_t driver;
	int error, pass;

	dmd = (struct driver_module_data *)arg;
	bus_devclass = devclass_find_internal(dmd->dmd_busname, NULL, TRUE);
	error = 0;

	switch (what) {
	case MOD_LOAD:
		if (dmd->dmd_chainevh)
			error = dmd->dmd_chainevh(mod,what,dmd->dmd_chainarg);

		pass = dmd->dmd_pass;
		driver = dmd->dmd_driver;
		PDEBUG(("Loading module: driver %s on bus %s (pass %d)",
		    DRIVERNAME(driver), dmd->dmd_busname, pass));
		error = devclass_add_driver(bus_devclass, driver, pass,
		    dmd->dmd_devclass);
		break;

	case MOD_UNLOAD:
		PDEBUG(("Unloading module: driver %s from bus %s",
		    DRIVERNAME(dmd->dmd_driver),
		    dmd->dmd_busname));
		error = devclass_delete_driver(bus_devclass,
		    dmd->dmd_driver);

		if (!error && dmd->dmd_chainevh)
			error = dmd->dmd_chainevh(mod,what,dmd->dmd_chainarg);
		break;
	case MOD_QUIESCE:
		PDEBUG(("Quiesce module: driver %s from bus %s",
		    DRIVERNAME(dmd->dmd_driver),
		    dmd->dmd_busname));
		error = devclass_quiesce_driver(bus_devclass,
		    dmd->dmd_driver);

		if (!error && dmd->dmd_chainevh)
			error = dmd->dmd_chainevh(mod,what,dmd->dmd_chainarg);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/**
 * @brief Enumerate all hinted devices for this bus.
 *
 * Walks through the hints for this bus and calls the bus_hinted_child
 * routine for each one it fines.  It searches first for the specific
 * bus that's being probed for hinted children (eg isa0), and then for
 * generic children (eg isa).
 *
 * @param	dev	bus device to enumerate
 */
void
bus_enumerate_hinted_children(device_t bus)
{
	int i;
	const char *dname, *busname;
	int dunit;

	/*
	 * enumerate all devices on the specific bus
	 */
	busname = device_get_nameunit(bus);
	i = 0;
	while (resource_find_match(&i, &dname, &dunit, "at", busname) == 0)
		BUS_HINTED_CHILD(bus, dname, dunit);

	/*
	 * and all the generic ones.
	 */
	busname = device_get_name(bus);
	i = 0;
	while (resource_find_match(&i, &dname, &dunit, "at", busname) == 0)
		BUS_HINTED_CHILD(bus, dname, dunit);
}

#ifdef BUS_DEBUG

/* the _short versions avoid iteration by not calling anything that prints
 * more than oneliners. I love oneliners.
 */

static void
print_device_short(device_t dev, int indent)
{
	if (!dev)
		return;

	indentprintf(("device %d: <%s> %sparent,%schildren,%s%s%s%s%s%s,%sivars,%ssoftc,busy=%d\n",
	    dev->unit, dev->desc,
	    (dev->parent? "":"no "),
	    (TAILQ_EMPTY(&dev->children)? "no ":""),
	    (dev->flags&DF_ENABLED? "enabled,":"disabled,"),
	    (dev->flags&DF_FIXEDCLASS? "fixed,":""),
	    (dev->flags&DF_WILDCARD? "wildcard,":""),
	    (dev->flags&DF_DESCMALLOCED? "descmalloced,":""),
	    (dev->flags&DF_REBID? "rebiddable,":""),
	    (dev->flags&DF_SUSPENDED? "suspended,":""),
	    (dev->ivars? "":"no "),
	    (dev->softc? "":"no "),
	    dev->busy));
}

static void
print_device(device_t dev, int indent)
{
	if (!dev)
		return;

	print_device_short(dev, indent);

	indentprintf(("Parent:\n"));
	print_device_short(dev->parent, indent+1);
	indentprintf(("Driver:\n"));
	print_driver_short(dev->driver, indent+1);
	indentprintf(("Devclass:\n"));
	print_devclass_short(dev->devclass, indent+1);
}

void
print_device_tree_short(device_t dev, int indent)
/* print the device and all its children (indented) */
{
	device_t child;

	if (!dev)
		return;

	print_device_short(dev, indent);

	TAILQ_FOREACH(child, &dev->children, link) {
		print_device_tree_short(child, indent+1);
	}
}

void
print_device_tree(device_t dev, int indent)
/* print the device and all its children (indented) */
{
	device_t child;

	if (!dev)
		return;

	print_device(dev, indent);

	TAILQ_FOREACH(child, &dev->children, link) {
		print_device_tree(child, indent+1);
	}
}

static void
print_driver_short(driver_t *driver, int indent)
{
	if (!driver)
		return;

	indentprintf(("driver %s: softc size = %zd\n",
	    driver->name, driver->size));
}

static void
print_driver(driver_t *driver, int indent)
{
	if (!driver)
		return;

	print_driver_short(driver, indent);
}

static void
print_driver_list(driver_list_t drivers, int indent)
{
	driverlink_t driver;

	TAILQ_FOREACH(driver, &drivers, link) {
		print_driver(driver->driver, indent);
	}
}

static void
print_devclass_short(devclass_t dc, int indent)
{
	if ( !dc )
		return;

	indentprintf(("devclass %s: max units = %d\n", dc->name, dc->maxunit));
}

static void
print_devclass(devclass_t dc, int indent)
{
	int i;

	if ( !dc )
		return;

	print_devclass_short(dc, indent);
	indentprintf(("Drivers:\n"));
	print_driver_list(dc->drivers, indent+1);

	indentprintf(("Devices:\n"));
	for (i = 0; i < dc->maxunit; i++)
		if (dc->devices[i])
			print_device(dc->devices[i], indent+1);
}

void
print_devclass_list_short(void)
{
	devclass_t dc;

	printf("Short listing of devclasses, drivers & devices:\n");
	TAILQ_FOREACH(dc, &devclasses, link) {
		print_devclass_short(dc, 0);
	}
}

void
print_devclass_list(void)
{
	devclass_t dc;

	printf("Full listing of devclasses, drivers & devices:\n");
	TAILQ_FOREACH(dc, &devclasses, link) {
		print_devclass(dc, 0);
	}
}

#endif

/*
 * User-space access to the device tree.
 *
 * We implement a small set of nodes:
 *
 * hw.bus			Single integer read method to obtain the
 *				current generation count.
 * hw.bus.devices		Reads the entire device tree in flat space.
 * hw.bus.rman			Resource manager interface
 *
 * We might like to add the ability to scan devclasses and/or drivers to
 * determine what else is currently loaded/available.
 */

static int
sysctl_bus(SYSCTL_HANDLER_ARGS)
{
	struct u_businfo	ubus;

	ubus.ub_version = BUS_USER_VERSION;
	ubus.ub_generation = bus_data_generation;

	return (SYSCTL_OUT(req, &ubus, sizeof(ubus)));
}
SYSCTL_NODE(_hw_bus, OID_AUTO, info, CTLFLAG_RW, sysctl_bus,
    "bus-related data");

static int
sysctl_devices(SYSCTL_HANDLER_ARGS)
{
	int			*name = (int *)arg1;
	u_int			namelen = arg2;
	int			index;
	device_t		dev;
	struct u_device		*udev;
	int			error;
	char			*walker, *ep;

	if (namelen != 2)
		return (EINVAL);

	if (bus_data_generation_check(name[0]))
		return (EINVAL);

	index = name[1];

	/*
	 * Scan the list of devices, looking for the requested index.
	 */
	TAILQ_FOREACH(dev, &bus_data_devices, devlink) {
		if (index-- == 0)
			break;
	}
	if (dev == NULL)
		return (ENOENT);

	/*
	 * Populate the return item, careful not to overflow the buffer.
	 */
	udev = malloc(sizeof(*udev), M_BUS, M_WAITOK | M_ZERO);
	if (udev == NULL)
		return (ENOMEM);
	udev->dv_handle = (uintptr_t)dev;
	udev->dv_parent = (uintptr_t)dev->parent;
	udev->dv_devflags = dev->devflags;
	udev->dv_flags = dev->flags;
	udev->dv_state = dev->state;
	walker = udev->dv_fields;
	ep = walker + sizeof(udev->dv_fields);
#define CP(src)						\
	if ((src) == NULL)				\
		*walker++ = '\0';			\
	else {						\
		strlcpy(walker, (src), ep - walker);	\
		walker += strlen(walker) + 1;		\
	}						\
	if (walker >= ep)				\
		break;

	do {
		CP(dev->nameunit);
		CP(dev->desc);
		CP(dev->driver != NULL ? dev->driver->name : NULL);
		bus_child_pnpinfo_str(dev, walker, ep - walker);
		walker += strlen(walker) + 1;
		if (walker >= ep)
			break;
		bus_child_location_str(dev, walker, ep - walker);
		walker += strlen(walker) + 1;
		if (walker >= ep)
			break;
		*walker++ = '\0';
	} while (0);
#undef CP
	error = SYSCTL_OUT(req, udev, sizeof(*udev));
	free(udev, M_BUS);
	return (error);
}

SYSCTL_NODE(_hw_bus, OID_AUTO, devices, CTLFLAG_RD, sysctl_devices,
    "system device tree");

int
bus_data_generation_check(int generation)
{
	if (generation != bus_data_generation)
		return (1);

	/* XXX generate optimised lists here? */
	return (0);
}

void
bus_data_generation_update(void)
{
	bus_data_generation++;
}

int
bus_free_resource(device_t dev, int type, struct resource *r)
{
	if (r == NULL)
		return (0);
	return (bus_release_resource(dev, type, rman_get_rid(r), r));
}

device_t
device_lookup_by_name(const char *name)
{
	device_t dev;

	TAILQ_FOREACH(dev, &bus_data_devices, devlink) {
		if (dev->nameunit != NULL && strcmp(dev->nameunit, name) == 0)
			return (dev);
	}
	return (NULL);
}

/*
 * /dev/devctl2 implementation.  The existing /dev/devctl device has
 * implicit semantics on open, so it could not be reused for this.
 * Another option would be to call this /dev/bus?
 */
static int
find_device(struct devreq *req, device_t *devp)
{
	device_t dev;

	/*
	 * First, ensure that the name is nul terminated.
	 */
	if (memchr(req->dr_name, '\0', sizeof(req->dr_name)) == NULL)
		return (EINVAL);

	/*
	 * Second, try to find an attached device whose name matches
	 * 'name'.
	 */
	dev = device_lookup_by_name(req->dr_name);
	if (dev != NULL) {
		*devp = dev;
		return (0);
	}

	/* Finally, give device enumerators a chance. */
	dev = NULL;
	EVENTHANDLER_DIRECT_INVOKE(dev_lookup, req->dr_name, &dev);
	if (dev == NULL)
		return (ENOENT);
	*devp = dev;
	return (0);
}

static bool
driver_exists(device_t bus, const char *driver)
{
	devclass_t dc;

	for (dc = bus->devclass; dc != NULL; dc = dc->parent) {
		if (devclass_find_driver_internal(dc, driver) != NULL)
			return (true);
	}
	return (false);
}

static void
device_gen_nomatch(device_t dev)
{
	device_t child;

	if (dev->flags & DF_NEEDNOMATCH &&
	    dev->state == DS_NOTPRESENT) {
		BUS_PROBE_NOMATCH(dev->parent, dev);
		devnomatch(dev);
		dev->flags |= DF_DONENOMATCH;
	}
	dev->flags &= ~DF_NEEDNOMATCH;
	TAILQ_FOREACH(child, &dev->children, link) {
		device_gen_nomatch(child);
	}
}

static void
device_do_deferred_actions(void)
{
	devclass_t dc;
	driverlink_t dl;

	/*
	 * Walk through the devclasses to find all the drivers we've tagged as
	 * deferred during the freeze and call the driver added routines. They
	 * have already been added to the lists in the background, so the driver
	 * added routines that trigger a probe will have all the right bidders
	 * for the probe auction.
	 */
	TAILQ_FOREACH(dc, &devclasses, link) {
		TAILQ_FOREACH(dl, &dc->drivers, link) {
			if (dl->flags & DL_DEFERRED_PROBE) {
				devclass_driver_added(dc, dl->driver);
				dl->flags &= ~DL_DEFERRED_PROBE;
			}
		}
	}

	/*
	 * We also defer no-match events during a freeze. Walk the tree and
	 * generate all the pent-up events that are still relevant.
	 */
	device_gen_nomatch(root_bus);
	bus_data_generation_update();
}

static int
devctl2_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct devreq *req;
	device_t dev;
	int error, old;

	/* Locate the device to control. */
	mtx_lock(&Giant);
	req = (struct devreq *)data;
	switch (cmd) {
	case DEV_ATTACH:
	case DEV_DETACH:
	case DEV_ENABLE:
	case DEV_DISABLE:
	case DEV_SUSPEND:
	case DEV_RESUME:
	case DEV_SET_DRIVER:
	case DEV_CLEAR_DRIVER:
	case DEV_RESCAN:
	case DEV_DELETE:
		error = priv_check(td, PRIV_DRIVER);
		if (error == 0)
			error = find_device(req, &dev);
		break;
	case DEV_FREEZE:
	case DEV_THAW:
		error = priv_check(td, PRIV_DRIVER);
		break;
	default:
		error = ENOTTY;
		break;
	}
	if (error) {
		mtx_unlock(&Giant);
		return (error);
	}

	/* Perform the requested operation. */
	switch (cmd) {
	case DEV_ATTACH:
		if (device_is_attached(dev) && (dev->flags & DF_REBID) == 0)
			error = EBUSY;
		else if (!device_is_enabled(dev))
			error = ENXIO;
		else
			error = device_probe_and_attach(dev);
		break;
	case DEV_DETACH:
		if (!device_is_attached(dev)) {
			error = ENXIO;
			break;
		}
		if (!(req->dr_flags & DEVF_FORCE_DETACH)) {
			error = device_quiesce(dev);
			if (error)
				break;
		}
		error = device_detach(dev);
		break;
	case DEV_ENABLE:
		if (device_is_enabled(dev)) {
			error = EBUSY;
			break;
		}

		/*
		 * If the device has been probed but not attached (e.g.
		 * when it has been disabled by a loader hint), just
		 * attach the device rather than doing a full probe.
		 */
		device_enable(dev);
		if (device_is_alive(dev)) {
			/*
			 * If the device was disabled via a hint, clear
			 * the hint.
			 */
			if (resource_disabled(dev->driver->name, dev->unit))
				resource_unset_value(dev->driver->name,
				    dev->unit, "disabled");
			error = device_attach(dev);
		} else
			error = device_probe_and_attach(dev);
		break;
	case DEV_DISABLE:
		if (!device_is_enabled(dev)) {
			error = ENXIO;
			break;
		}

		if (!(req->dr_flags & DEVF_FORCE_DETACH)) {
			error = device_quiesce(dev);
			if (error)
				break;
		}

		/*
		 * Force DF_FIXEDCLASS on around detach to preserve
		 * the existing name.
		 */
		old = dev->flags;
		dev->flags |= DF_FIXEDCLASS;
		error = device_detach(dev);
		if (!(old & DF_FIXEDCLASS))
			dev->flags &= ~DF_FIXEDCLASS;
		if (error == 0)
			device_disable(dev);
		break;
	case DEV_SUSPEND:
		if (device_is_suspended(dev)) {
			error = EBUSY;
			break;
		}
		if (device_get_parent(dev) == NULL) {
			error = EINVAL;
			break;
		}
		error = BUS_SUSPEND_CHILD(device_get_parent(dev), dev);
		break;
	case DEV_RESUME:
		if (!device_is_suspended(dev)) {
			error = EINVAL;
			break;
		}
		if (device_get_parent(dev) == NULL) {
			error = EINVAL;
			break;
		}
		error = BUS_RESUME_CHILD(device_get_parent(dev), dev);
		break;
	case DEV_SET_DRIVER: {
		devclass_t dc;
		char driver[128];

		error = copyinstr(req->dr_data, driver, sizeof(driver), NULL);
		if (error)
			break;
		if (driver[0] == '\0') {
			error = EINVAL;
			break;
		}
		if (dev->devclass != NULL &&
		    strcmp(driver, dev->devclass->name) == 0)
			/* XXX: Could possibly force DF_FIXEDCLASS on? */
			break;

		/*
		 * Scan drivers for this device's bus looking for at
		 * least one matching driver.
		 */
		if (dev->parent == NULL) {
			error = EINVAL;
			break;
		}
		if (!driver_exists(dev->parent, driver)) {
			error = ENOENT;
			break;
		}
		dc = devclass_create(driver);
		if (dc == NULL) {
			error = ENOMEM;
			break;
		}

		/* Detach device if necessary. */
		if (device_is_attached(dev)) {
			if (req->dr_flags & DEVF_SET_DRIVER_DETACH)
				error = device_detach(dev);
			else
				error = EBUSY;
			if (error)
				break;
		}

		/* Clear any previously-fixed device class and unit. */
		if (dev->flags & DF_FIXEDCLASS)
			devclass_delete_device(dev->devclass, dev);
		dev->flags |= DF_WILDCARD;
		dev->unit = -1;

		/* Force the new device class. */
		error = devclass_add_device(dc, dev);
		if (error)
			break;
		dev->flags |= DF_FIXEDCLASS;
		error = device_probe_and_attach(dev);
		break;
	}
	case DEV_CLEAR_DRIVER:
		if (!(dev->flags & DF_FIXEDCLASS)) {
			error = 0;
			break;
		}
		if (device_is_attached(dev)) {
			if (req->dr_flags & DEVF_CLEAR_DRIVER_DETACH)
				error = device_detach(dev);
			else
				error = EBUSY;
			if (error)
				break;
		}

		dev->flags &= ~DF_FIXEDCLASS;
		dev->flags |= DF_WILDCARD;
		devclass_delete_device(dev->devclass, dev);
		error = device_probe_and_attach(dev);
		break;
	case DEV_RESCAN:
		if (!device_is_attached(dev)) {
			error = ENXIO;
			break;
		}
		error = BUS_RESCAN(dev);
		break;
	case DEV_DELETE: {
		device_t parent;

		parent = device_get_parent(dev);
		if (parent == NULL) {
			error = EINVAL;
			break;
		}
		if (!(req->dr_flags & DEVF_FORCE_DELETE)) {
			if (bus_child_present(dev) != 0) {
				error = EBUSY;
				break;
			}
		}
		
		error = device_delete_child(parent, dev);
		break;
	}
	case DEV_FREEZE:
		if (device_frozen)
			error = EBUSY;
		else
			device_frozen = true;
		break;
	case DEV_THAW:
		if (!device_frozen)
			error = EBUSY;
		else {
			device_do_deferred_actions();
			device_frozen = false;
		}
		break;
	}
	mtx_unlock(&Giant);
	return (error);
}

static struct cdevsw devctl2_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	devctl2_ioctl,
	.d_name =	"devctl2",
};

static void
devctl2_init(void)
{

	make_dev_credf(MAKEDEV_ETERNAL, &devctl2_cdevsw, 0, NULL,
	    UID_ROOT, GID_WHEEL, 0600, "devctl2");
}

/*
 * APIs to manage deprecation and obsolescence.
 */
static int obsolete_panic = 0;
SYSCTL_INT(_debug, OID_AUTO, obsolete_panic, CTLFLAG_RWTUN, &obsolete_panic, 0,
    "Bus debug level");
/* 0 - don't panic, 1 - panic if already obsolete, 2 - panic if deprecated */
static void
gone_panic(int major, int running, const char *msg)
{

	switch (obsolete_panic)
	{
	case 0:
		return;
	case 1:
		if (running < major)
			return;
		/* FALLTHROUGH */
	default:
		panic("%s", msg);
	}
}

void
_gone_in(int major, const char *msg)
{

	gone_panic(major, P_OSREL_MAJOR(__FreeBSD_version), msg);
	if (P_OSREL_MAJOR(__FreeBSD_version) >= major)
		printf("Obsolete code will removed soon: %s\n", msg);
	else if (P_OSREL_MAJOR(__FreeBSD_version) + 1 == major)
		printf("Deprecated code (to be removed in FreeBSD %d): %s\n",
		    major, msg);
}

void
_gone_in_dev(device_t dev, int major, const char *msg)
{

	gone_panic(major, P_OSREL_MAJOR(__FreeBSD_version), msg);
	if (P_OSREL_MAJOR(__FreeBSD_version) >= major)
		device_printf(dev,
		    "Obsolete code will removed soon: %s\n", msg);
	else if (P_OSREL_MAJOR(__FreeBSD_version) + 1 == major)
		device_printf(dev,
		    "Deprecated code (to be removed in FreeBSD %d): %s\n",
		    major, msg);
}

#ifdef DDB
DB_SHOW_COMMAND(device, db_show_device)
{
	device_t dev;

	if (!have_addr)
		return;

	dev = (device_t)addr;

	db_printf("name:    %s\n", device_get_nameunit(dev));
	db_printf("  driver:  %s\n", DRIVERNAME(dev->driver));
	db_printf("  class:   %s\n", DEVCLANAME(dev->devclass));
	db_printf("  addr:    %p\n", dev);
	db_printf("  parent:  %p\n", dev->parent);
	db_printf("  softc:   %p\n", dev->softc);
	db_printf("  ivars:   %p\n", dev->ivars);
}

DB_SHOW_ALL_COMMAND(devices, db_show_all_devices)
{
	device_t dev;

	TAILQ_FOREACH(dev, &bus_data_devices, devlink) {
		db_show_device((db_expr_t)dev, true, count, modif);
	}
}
#endif
