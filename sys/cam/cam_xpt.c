/*-
 * Implementation of the Common Access Method Transport (XPT) layer.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 1999 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_printf.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>

#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/kthread.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_iosched.h>
#include <cam/cam_periph.h>
#include <cam/cam_queue.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_debug.h>
#include <cam/cam_compat.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

#include <machine/md_var.h>	/* geometry translation */
#include <machine/stdarg.h>	/* for xpt_print below */

#include "opt_cam.h"

/* Wild guess based on not wanting to grow the stack too much */
#define XPT_PRINT_MAXLEN	512
#ifdef PRINTF_BUFR_SIZE
#define XPT_PRINT_LEN	PRINTF_BUFR_SIZE
#else
#define XPT_PRINT_LEN	128
#endif
_Static_assert(XPT_PRINT_LEN <= XPT_PRINT_MAXLEN, "XPT_PRINT_LEN is too large");

/*
 * This is the maximum number of high powered commands (e.g. start unit)
 * that can be outstanding at a particular time.
 */
#ifndef CAM_MAX_HIGHPOWER
#define CAM_MAX_HIGHPOWER  4
#endif

/* Datastructures internal to the xpt layer */
MALLOC_DEFINE(M_CAMXPT, "CAM XPT", "CAM XPT buffers");
MALLOC_DEFINE(M_CAMDEV, "CAM DEV", "CAM devices");
MALLOC_DEFINE(M_CAMCCB, "CAM CCB", "CAM CCBs");
MALLOC_DEFINE(M_CAMPATH, "CAM path", "CAM paths");

/* Object for defering XPT actions to a taskqueue */
struct xpt_task {
	struct task	task;
	void		*data1;
	uintptr_t	data2;
};

struct xpt_softc {
	uint32_t		xpt_generation;

	/* number of high powered commands that can go through right now */
	struct mtx		xpt_highpower_lock;
	STAILQ_HEAD(highpowerlist, cam_ed)	highpowerq;
	int			num_highpower;

	/* queue for handling async rescan requests. */
	TAILQ_HEAD(, ccb_hdr) ccb_scanq;
	int buses_to_config;
	int buses_config_done;
	int announce_nosbuf;

	/*
	 * Registered buses
	 *
	 * N.B., "busses" is an archaic spelling of "buses".  In new code
	 * "buses" is preferred.
	 */
	TAILQ_HEAD(,cam_eb)	xpt_busses;
	u_int			bus_generation;

	struct intr_config_hook	xpt_config_hook;

	int			boot_delay;
	struct callout 		boot_callout;

	struct mtx		xpt_topo_lock;
	struct mtx		xpt_lock;
	struct taskqueue	*xpt_taskq;
};

typedef enum {
	DM_RET_COPY		= 0x01,
	DM_RET_FLAG_MASK	= 0x0f,
	DM_RET_NONE		= 0x00,
	DM_RET_STOP		= 0x10,
	DM_RET_DESCEND		= 0x20,
	DM_RET_ERROR		= 0x30,
	DM_RET_ACTION_MASK	= 0xf0
} dev_match_ret;

typedef enum {
	XPT_DEPTH_BUS,
	XPT_DEPTH_TARGET,
	XPT_DEPTH_DEVICE,
	XPT_DEPTH_PERIPH
} xpt_traverse_depth;

struct xpt_traverse_config {
	xpt_traverse_depth	depth;
	void			*tr_func;
	void			*tr_arg;
};

typedef	int	xpt_busfunc_t (struct cam_eb *bus, void *arg);
typedef	int	xpt_targetfunc_t (struct cam_et *target, void *arg);
typedef	int	xpt_devicefunc_t (struct cam_ed *device, void *arg);
typedef	int	xpt_periphfunc_t (struct cam_periph *periph, void *arg);
typedef int	xpt_pdrvfunc_t (struct periph_driver **pdrv, void *arg);

/* Transport layer configuration information */
static struct xpt_softc xsoftc;

MTX_SYSINIT(xpt_topo_init, &xsoftc.xpt_topo_lock, "XPT topology lock", MTX_DEF);

SYSCTL_INT(_kern_cam, OID_AUTO, boot_delay, CTLFLAG_RDTUN,
           &xsoftc.boot_delay, 0, "Bus registration wait time");
SYSCTL_UINT(_kern_cam, OID_AUTO, xpt_generation, CTLFLAG_RD,
	    &xsoftc.xpt_generation, 0, "CAM peripheral generation count");
SYSCTL_INT(_kern_cam, OID_AUTO, announce_nosbuf, CTLFLAG_RWTUN,
	    &xsoftc.announce_nosbuf, 0, "Don't use sbuf for announcements");

struct cam_doneq {
	struct mtx_padalign	cam_doneq_mtx;
	STAILQ_HEAD(, ccb_hdr)	cam_doneq;
	int			cam_doneq_sleep;
};

static struct cam_doneq cam_doneqs[MAXCPU];
static int cam_num_doneqs;
static struct proc *cam_proc;

SYSCTL_INT(_kern_cam, OID_AUTO, num_doneqs, CTLFLAG_RDTUN,
           &cam_num_doneqs, 0, "Number of completion queues/threads");

struct cam_periph *xpt_periph;

static periph_init_t xpt_periph_init;

static struct periph_driver xpt_driver =
{
	xpt_periph_init, "xpt",
	TAILQ_HEAD_INITIALIZER(xpt_driver.units), /* generation */ 0,
	CAM_PERIPH_DRV_EARLY
};

PERIPHDRIVER_DECLARE(xpt, xpt_driver);

static d_open_t xptopen;
static d_close_t xptclose;
static d_ioctl_t xptioctl;
static d_ioctl_t xptdoioctl;

static struct cdevsw xpt_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	0,
	.d_open =	xptopen,
	.d_close =	xptclose,
	.d_ioctl =	xptioctl,
	.d_name =	"xpt",
};

/* Storage for debugging datastructures */
struct cam_path *cam_dpath;
u_int32_t cam_dflags = CAM_DEBUG_FLAGS;
SYSCTL_UINT(_kern_cam, OID_AUTO, dflags, CTLFLAG_RWTUN,
	&cam_dflags, 0, "Enabled debug flags");
u_int32_t cam_debug_delay = CAM_DEBUG_DELAY;
SYSCTL_UINT(_kern_cam, OID_AUTO, debug_delay, CTLFLAG_RWTUN,
	&cam_debug_delay, 0, "Delay in us after each debug message");

/* Our boot-time initialization hook */
static int cam_module_event_handler(module_t, int /*modeventtype_t*/, void *);

static moduledata_t cam_moduledata = {
	"cam",
	cam_module_event_handler,
	NULL
};

static int	xpt_init(void *);

DECLARE_MODULE(cam, cam_moduledata, SI_SUB_CONFIGURE, SI_ORDER_SECOND);
MODULE_VERSION(cam, 1);


static void		xpt_async_bcast(struct async_list *async_head,
					u_int32_t async_code,
					struct cam_path *path,
					void *async_arg);
static path_id_t xptnextfreepathid(void);
static path_id_t xptpathid(const char *sim_name, int sim_unit, int sim_bus);
static union ccb *xpt_get_ccb(struct cam_periph *periph);
static union ccb *xpt_get_ccb_nowait(struct cam_periph *periph);
static void	 xpt_run_allocq(struct cam_periph *periph, int sleep);
static void	 xpt_run_allocq_task(void *context, int pending);
static void	 xpt_run_devq(struct cam_devq *devq);
static timeout_t xpt_release_devq_timeout;
static void	 xpt_release_simq_timeout(void *arg) __unused;
static void	 xpt_acquire_bus(struct cam_eb *bus);
static void	 xpt_release_bus(struct cam_eb *bus);
static uint32_t	 xpt_freeze_devq_device(struct cam_ed *dev, u_int count);
static int	 xpt_release_devq_device(struct cam_ed *dev, u_int count,
		    int run_queue);
static struct cam_et*
		 xpt_alloc_target(struct cam_eb *bus, target_id_t target_id);
static void	 xpt_acquire_target(struct cam_et *target);
static void	 xpt_release_target(struct cam_et *target);
static struct cam_eb*
		 xpt_find_bus(path_id_t path_id);
static struct cam_et*
		 xpt_find_target(struct cam_eb *bus, target_id_t target_id);
static struct cam_ed*
		 xpt_find_device(struct cam_et *target, lun_id_t lun_id);
static void	 xpt_config(void *arg);
static int	 xpt_schedule_dev(struct camq *queue, cam_pinfo *dev_pinfo,
				 u_int32_t new_priority);
static xpt_devicefunc_t xptpassannouncefunc;
static void	 xptaction(struct cam_sim *sim, union ccb *work_ccb);
static void	 xptpoll(struct cam_sim *sim);
static void	 camisr_runqueue(void);
static void	 xpt_done_process(struct ccb_hdr *ccb_h);
static void	 xpt_done_td(void *);
static dev_match_ret	xptbusmatch(struct dev_match_pattern *patterns,
				    u_int num_patterns, struct cam_eb *bus);
static dev_match_ret	xptdevicematch(struct dev_match_pattern *patterns,
				       u_int num_patterns,
				       struct cam_ed *device);
static dev_match_ret	xptperiphmatch(struct dev_match_pattern *patterns,
				       u_int num_patterns,
				       struct cam_periph *periph);
static xpt_busfunc_t	xptedtbusfunc;
static xpt_targetfunc_t	xptedttargetfunc;
static xpt_devicefunc_t	xptedtdevicefunc;
static xpt_periphfunc_t	xptedtperiphfunc;
static xpt_pdrvfunc_t	xptplistpdrvfunc;
static xpt_periphfunc_t	xptplistperiphfunc;
static int		xptedtmatch(struct ccb_dev_match *cdm);
static int		xptperiphlistmatch(struct ccb_dev_match *cdm);
static int		xptbustraverse(struct cam_eb *start_bus,
				       xpt_busfunc_t *tr_func, void *arg);
static int		xpttargettraverse(struct cam_eb *bus,
					  struct cam_et *start_target,
					  xpt_targetfunc_t *tr_func, void *arg);
static int		xptdevicetraverse(struct cam_et *target,
					  struct cam_ed *start_device,
					  xpt_devicefunc_t *tr_func, void *arg);
static int		xptperiphtraverse(struct cam_ed *device,
					  struct cam_periph *start_periph,
					  xpt_periphfunc_t *tr_func, void *arg);
static int		xptpdrvtraverse(struct periph_driver **start_pdrv,
					xpt_pdrvfunc_t *tr_func, void *arg);
static int		xptpdperiphtraverse(struct periph_driver **pdrv,
					    struct cam_periph *start_periph,
					    xpt_periphfunc_t *tr_func,
					    void *arg);
static xpt_busfunc_t	xptdefbusfunc;
static xpt_targetfunc_t	xptdeftargetfunc;
static xpt_devicefunc_t	xptdefdevicefunc;
static xpt_periphfunc_t	xptdefperiphfunc;
static void		xpt_finishconfig_task(void *context, int pending);
static void		xpt_dev_async_default(u_int32_t async_code,
					      struct cam_eb *bus,
					      struct cam_et *target,
					      struct cam_ed *device,
					      void *async_arg);
static struct cam_ed *	xpt_alloc_device_default(struct cam_eb *bus,
						 struct cam_et *target,
						 lun_id_t lun_id);
static xpt_devicefunc_t	xptsetasyncfunc;
static xpt_busfunc_t	xptsetasyncbusfunc;
static cam_status	xptregister(struct cam_periph *periph,
				    void *arg);
static __inline int device_is_queued(struct cam_ed *device);

static __inline int
xpt_schedule_devq(struct cam_devq *devq, struct cam_ed *dev)
{
	int	retval;

	mtx_assert(&devq->send_mtx, MA_OWNED);
	if ((dev->ccbq.queue.entries > 0) &&
	    (dev->ccbq.dev_openings > 0) &&
	    (dev->ccbq.queue.qfrozen_cnt == 0)) {
		/*
		 * The priority of a device waiting for controller
		 * resources is that of the highest priority CCB
		 * enqueued.
		 */
		retval =
		    xpt_schedule_dev(&devq->send_queue,
				     &dev->devq_entry,
				     CAMQ_GET_PRIO(&dev->ccbq.queue));
	} else {
		retval = 0;
	}
	return (retval);
}

static __inline int
device_is_queued(struct cam_ed *device)
{
	return (device->devq_entry.index != CAM_UNQUEUED_INDEX);
}

static void
xpt_periph_init()
{
	make_dev(&xpt_cdevsw, 0, UID_ROOT, GID_OPERATOR, 0600, "xpt0");
}

static int
xptopen(struct cdev *dev, int flags, int fmt, struct thread *td)
{

	/*
	 * Only allow read-write access.
	 */
	if (((flags & FWRITE) == 0) || ((flags & FREAD) == 0))
		return(EPERM);

	/*
	 * We don't allow nonblocking access.
	 */
	if ((flags & O_NONBLOCK) != 0) {
		printf("%s: can't do nonblocking access\n", devtoname(dev));
		return(ENODEV);
	}

	return(0);
}

static int
xptclose(struct cdev *dev, int flag, int fmt, struct thread *td)
{

	return(0);
}

/*
 * Don't automatically grab the xpt softc lock here even though this is going
 * through the xpt device.  The xpt device is really just a back door for
 * accessing other devices and SIMs, so the right thing to do is to grab
 * the appropriate SIM lock once the bus/SIM is located.
 */
static int
xptioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	int error;

	if ((error = xptdoioctl(dev, cmd, addr, flag, td)) == ENOTTY) {
		error = cam_compat_ioctl(dev, cmd, addr, flag, td, xptdoioctl);
	}
	return (error);
}

static int
xptdoioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag, struct thread *td)
{
	int error;

	error = 0;

	switch(cmd) {
	/*
	 * For the transport layer CAMIOCOMMAND ioctl, we really only want
	 * to accept CCB types that don't quite make sense to send through a
	 * passthrough driver. XPT_PATH_INQ is an exception to this, as stated
	 * in the CAM spec.
	 */
	case CAMIOCOMMAND: {
		union ccb *ccb;
		union ccb *inccb;
		struct cam_eb *bus;

		inccb = (union ccb *)addr;
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
		if (inccb->ccb_h.func_code == XPT_SCSI_IO)
			inccb->csio.bio = NULL;
#endif

		if (inccb->ccb_h.flags & CAM_UNLOCKED)
			return (EINVAL);

		bus = xpt_find_bus(inccb->ccb_h.path_id);
		if (bus == NULL)
			return (EINVAL);

		switch (inccb->ccb_h.func_code) {
		case XPT_SCAN_BUS:
		case XPT_RESET_BUS:
			if (inccb->ccb_h.target_id != CAM_TARGET_WILDCARD ||
			    inccb->ccb_h.target_lun != CAM_LUN_WILDCARD) {
				xpt_release_bus(bus);
				return (EINVAL);
			}
			break;
		case XPT_SCAN_TGT:
			if (inccb->ccb_h.target_id == CAM_TARGET_WILDCARD ||
			    inccb->ccb_h.target_lun != CAM_LUN_WILDCARD) {
				xpt_release_bus(bus);
				return (EINVAL);
			}
			break;
		default:
			break;
		}

		switch(inccb->ccb_h.func_code) {
		case XPT_SCAN_BUS:
		case XPT_RESET_BUS:
		case XPT_PATH_INQ:
		case XPT_ENG_INQ:
		case XPT_SCAN_LUN:
		case XPT_SCAN_TGT:

			ccb = xpt_alloc_ccb();

			/*
			 * Create a path using the bus, target, and lun the
			 * user passed in.
			 */
			if (xpt_create_path(&ccb->ccb_h.path, NULL,
					    inccb->ccb_h.path_id,
					    inccb->ccb_h.target_id,
					    inccb->ccb_h.target_lun) !=
					    CAM_REQ_CMP){
				error = EINVAL;
				xpt_free_ccb(ccb);
				break;
			}
			/* Ensure all of our fields are correct */
			xpt_setup_ccb(&ccb->ccb_h, ccb->ccb_h.path,
				      inccb->ccb_h.pinfo.priority);
			xpt_merge_ccb(ccb, inccb);
			xpt_path_lock(ccb->ccb_h.path);
			cam_periph_runccb(ccb, NULL, 0, 0, NULL);
			xpt_path_unlock(ccb->ccb_h.path);
			bcopy(ccb, inccb, sizeof(union ccb));
			xpt_free_path(ccb->ccb_h.path);
			xpt_free_ccb(ccb);
			break;

		case XPT_DEBUG: {
			union ccb ccb;

			/*
			 * This is an immediate CCB, so it's okay to
			 * allocate it on the stack.
			 */

			/*
			 * Create a path using the bus, target, and lun the
			 * user passed in.
			 */
			if (xpt_create_path(&ccb.ccb_h.path, NULL,
					    inccb->ccb_h.path_id,
					    inccb->ccb_h.target_id,
					    inccb->ccb_h.target_lun) !=
					    CAM_REQ_CMP){
				error = EINVAL;
				break;
			}
			/* Ensure all of our fields are correct */
			xpt_setup_ccb(&ccb.ccb_h, ccb.ccb_h.path,
				      inccb->ccb_h.pinfo.priority);
			xpt_merge_ccb(&ccb, inccb);
			xpt_action(&ccb);
			bcopy(&ccb, inccb, sizeof(union ccb));
			xpt_free_path(ccb.ccb_h.path);
			break;

		}
		case XPT_DEV_MATCH: {
			struct cam_periph_map_info mapinfo;
			struct cam_path *old_path;

			/*
			 * We can't deal with physical addresses for this
			 * type of transaction.
			 */
			if ((inccb->ccb_h.flags & CAM_DATA_MASK) !=
			    CAM_DATA_VADDR) {
				error = EINVAL;
				break;
			}

			/*
			 * Save this in case the caller had it set to
			 * something in particular.
			 */
			old_path = inccb->ccb_h.path;

			/*
			 * We really don't need a path for the matching
			 * code.  The path is needed because of the
			 * debugging statements in xpt_action().  They
			 * assume that the CCB has a valid path.
			 */
			inccb->ccb_h.path = xpt_periph->path;

			bzero(&mapinfo, sizeof(mapinfo));

			/*
			 * Map the pattern and match buffers into kernel
			 * virtual address space.
			 */
			error = cam_periph_mapmem(inccb, &mapinfo, MAXPHYS);

			if (error) {
				inccb->ccb_h.path = old_path;
				break;
			}

			/*
			 * This is an immediate CCB, we can send it on directly.
			 */
			xpt_action(inccb);

			/*
			 * Map the buffers back into user space.
			 */
			cam_periph_unmapmem(inccb, &mapinfo);

			inccb->ccb_h.path = old_path;

			error = 0;
			break;
		}
		default:
			error = ENOTSUP;
			break;
		}
		xpt_release_bus(bus);
		break;
	}
	/*
	 * This is the getpassthru ioctl. It takes a XPT_GDEVLIST ccb as input,
	 * with the periphal driver name and unit name filled in.  The other
	 * fields don't really matter as input.  The passthrough driver name
	 * ("pass"), and unit number are passed back in the ccb.  The current
	 * device generation number, and the index into the device peripheral
	 * driver list, and the status are also passed back.  Note that
	 * since we do everything in one pass, unlike the XPT_GDEVLIST ccb,
	 * we never return a status of CAM_GDEVLIST_LIST_CHANGED.  It is
	 * (or rather should be) impossible for the device peripheral driver
	 * list to change since we look at the whole thing in one pass, and
	 * we do it with lock protection.
	 *
	 */
	case CAMGETPASSTHRU: {
		union ccb *ccb;
		struct cam_periph *periph;
		struct periph_driver **p_drv;
		char   *name;
		u_int unit;
		int base_periph_found;

		ccb = (union ccb *)addr;
		unit = ccb->cgdl.unit_number;
		name = ccb->cgdl.periph_name;
		base_periph_found = 0;
#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
		if (ccb->ccb_h.func_code == XPT_SCSI_IO)
			ccb->csio.bio = NULL;
#endif

		/*
		 * Sanity check -- make sure we don't get a null peripheral
		 * driver name.
		 */
		if (*ccb->cgdl.periph_name == '\0') {
			error = EINVAL;
			break;
		}

		/* Keep the list from changing while we traverse it */
		xpt_lock_buses();

		/* first find our driver in the list of drivers */
		for (p_drv = periph_drivers; *p_drv != NULL; p_drv++)
			if (strcmp((*p_drv)->driver_name, name) == 0)
				break;

		if (*p_drv == NULL) {
			xpt_unlock_buses();
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			ccb->cgdl.status = CAM_GDEVLIST_ERROR;
			*ccb->cgdl.periph_name = '\0';
			ccb->cgdl.unit_number = 0;
			error = ENOENT;
			break;
		}

		/*
		 * Run through every peripheral instance of this driver
		 * and check to see whether it matches the unit passed
		 * in by the user.  If it does, get out of the loops and
		 * find the passthrough driver associated with that
		 * peripheral driver.
		 */
		for (periph = TAILQ_FIRST(&(*p_drv)->units); periph != NULL;
		     periph = TAILQ_NEXT(periph, unit_links)) {

			if (periph->unit_number == unit)
				break;
		}
		/*
		 * If we found the peripheral driver that the user passed
		 * in, go through all of the peripheral drivers for that
		 * particular device and look for a passthrough driver.
		 */
		if (periph != NULL) {
			struct cam_ed *device;
			int i;

			base_periph_found = 1;
			device = periph->path->device;
			for (i = 0, periph = SLIST_FIRST(&device->periphs);
			     periph != NULL;
			     periph = SLIST_NEXT(periph, periph_links), i++) {
				/*
				 * Check to see whether we have a
				 * passthrough device or not.
				 */
				if (strcmp(periph->periph_name, "pass") == 0) {
					/*
					 * Fill in the getdevlist fields.
					 */
					strlcpy(ccb->cgdl.periph_name,
					       periph->periph_name,
					       sizeof(ccb->cgdl.periph_name));
					ccb->cgdl.unit_number =
						periph->unit_number;
					if (SLIST_NEXT(periph, periph_links))
						ccb->cgdl.status =
							CAM_GDEVLIST_MORE_DEVS;
					else
						ccb->cgdl.status =
						       CAM_GDEVLIST_LAST_DEVICE;
					ccb->cgdl.generation =
						device->generation;
					ccb->cgdl.index = i;
					/*
					 * Fill in some CCB header fields
					 * that the user may want.
					 */
					ccb->ccb_h.path_id =
						periph->path->bus->path_id;
					ccb->ccb_h.target_id =
						periph->path->target->target_id;
					ccb->ccb_h.target_lun =
						periph->path->device->lun_id;
					ccb->ccb_h.status = CAM_REQ_CMP;
					break;
				}
			}
		}

		/*
		 * If the periph is null here, one of two things has
		 * happened.  The first possibility is that we couldn't
		 * find the unit number of the particular peripheral driver
		 * that the user is asking about.  e.g. the user asks for
		 * the passthrough driver for "da11".  We find the list of
		 * "da" peripherals all right, but there is no unit 11.
		 * The other possibility is that we went through the list
		 * of peripheral drivers attached to the device structure,
		 * but didn't find one with the name "pass".  Either way,
		 * we return ENOENT, since we couldn't find something.
		 */
		if (periph == NULL) {
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			ccb->cgdl.status = CAM_GDEVLIST_ERROR;
			*ccb->cgdl.periph_name = '\0';
			ccb->cgdl.unit_number = 0;
			error = ENOENT;
			/*
			 * It is unfortunate that this is even necessary,
			 * but there are many, many clueless users out there.
			 * If this is true, the user is looking for the
			 * passthrough driver, but doesn't have one in his
			 * kernel.
			 */
			if (base_periph_found == 1) {
				printf("xptioctl: pass driver is not in the "
				       "kernel\n");
				printf("xptioctl: put \"device pass\" in "
				       "your kernel config file\n");
			}
		}
		xpt_unlock_buses();
		break;
		}
	default:
		error = ENOTTY;
		break;
	}

	return(error);
}

static int
cam_module_event_handler(module_t mod, int what, void *arg)
{
	int error;

	switch (what) {
	case MOD_LOAD:
		if ((error = xpt_init(NULL)) != 0)
			return (error);
		break;
	case MOD_UNLOAD:
		return EBUSY;
	default:
		return EOPNOTSUPP;
	}

	return 0;
}

static struct xpt_proto *
xpt_proto_find(cam_proto proto)
{
	struct xpt_proto **pp;

	SET_FOREACH(pp, cam_xpt_proto_set) {
		if ((*pp)->proto == proto)
			return *pp;
	}

	return NULL;
}

static void
xpt_rescan_done(struct cam_periph *periph, union ccb *done_ccb)
{

	if (done_ccb->ccb_h.ppriv_ptr1 == NULL) {
		xpt_free_path(done_ccb->ccb_h.path);
		xpt_free_ccb(done_ccb);
	} else {
		done_ccb->ccb_h.cbfcnp = done_ccb->ccb_h.ppriv_ptr1;
		(*done_ccb->ccb_h.cbfcnp)(periph, done_ccb);
	}
	xpt_release_boot();
}

/* thread to handle bus rescans */
static void
xpt_scanner_thread(void *dummy)
{
	union ccb	*ccb;
	struct cam_path	 path;

	xpt_lock_buses();
	for (;;) {
		if (TAILQ_EMPTY(&xsoftc.ccb_scanq))
			msleep(&xsoftc.ccb_scanq, &xsoftc.xpt_topo_lock, PRIBIO,
			       "-", 0);
		if ((ccb = (union ccb *)TAILQ_FIRST(&xsoftc.ccb_scanq)) != NULL) {
			TAILQ_REMOVE(&xsoftc.ccb_scanq, &ccb->ccb_h, sim_links.tqe);
			xpt_unlock_buses();

			/*
			 * Since lock can be dropped inside and path freed
			 * by completion callback even before return here,
			 * take our own path copy for reference.
			 */
			xpt_copy_path(&path, ccb->ccb_h.path);
			xpt_path_lock(&path);
			xpt_action(ccb);
			xpt_path_unlock(&path);
			xpt_release_path(&path);

			xpt_lock_buses();
		}
	}
}

void
xpt_rescan(union ccb *ccb)
{
	struct ccb_hdr *hdr;

	/* Prepare request */
	if (ccb->ccb_h.path->target->target_id == CAM_TARGET_WILDCARD &&
	    ccb->ccb_h.path->device->lun_id == CAM_LUN_WILDCARD)
		ccb->ccb_h.func_code = XPT_SCAN_BUS;
	else if (ccb->ccb_h.path->target->target_id != CAM_TARGET_WILDCARD &&
	    ccb->ccb_h.path->device->lun_id == CAM_LUN_WILDCARD)
		ccb->ccb_h.func_code = XPT_SCAN_TGT;
	else if (ccb->ccb_h.path->target->target_id != CAM_TARGET_WILDCARD &&
	    ccb->ccb_h.path->device->lun_id != CAM_LUN_WILDCARD)
		ccb->ccb_h.func_code = XPT_SCAN_LUN;
	else {
		xpt_print(ccb->ccb_h.path, "illegal scan path\n");
		xpt_free_path(ccb->ccb_h.path);
		xpt_free_ccb(ccb);
		return;
	}
	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_rescan: func %#x %s\n", ccb->ccb_h.func_code,
 		xpt_action_name(ccb->ccb_h.func_code)));

	ccb->ccb_h.ppriv_ptr1 = ccb->ccb_h.cbfcnp;
	ccb->ccb_h.cbfcnp = xpt_rescan_done;
	xpt_setup_ccb(&ccb->ccb_h, ccb->ccb_h.path, CAM_PRIORITY_XPT);
	/* Don't make duplicate entries for the same paths. */
	xpt_lock_buses();
	if (ccb->ccb_h.ppriv_ptr1 == NULL) {
		TAILQ_FOREACH(hdr, &xsoftc.ccb_scanq, sim_links.tqe) {
			if (xpt_path_comp(hdr->path, ccb->ccb_h.path) == 0) {
				wakeup(&xsoftc.ccb_scanq);
				xpt_unlock_buses();
				xpt_print(ccb->ccb_h.path, "rescan already queued\n");
				xpt_free_path(ccb->ccb_h.path);
				xpt_free_ccb(ccb);
				return;
			}
		}
	}
	TAILQ_INSERT_TAIL(&xsoftc.ccb_scanq, &ccb->ccb_h, sim_links.tqe);
	xsoftc.buses_to_config++;
	wakeup(&xsoftc.ccb_scanq);
	xpt_unlock_buses();
}

/* Functions accessed by the peripheral drivers */
static int
xpt_init(void *dummy)
{
	struct cam_sim *xpt_sim;
	struct cam_path *path;
	struct cam_devq *devq;
	cam_status status;
	int error, i;

	TAILQ_INIT(&xsoftc.xpt_busses);
	TAILQ_INIT(&xsoftc.ccb_scanq);
	STAILQ_INIT(&xsoftc.highpowerq);
	xsoftc.num_highpower = CAM_MAX_HIGHPOWER;

	mtx_init(&xsoftc.xpt_lock, "XPT lock", NULL, MTX_DEF);
	mtx_init(&xsoftc.xpt_highpower_lock, "XPT highpower lock", NULL, MTX_DEF);
	xsoftc.xpt_taskq = taskqueue_create("CAM XPT task", M_WAITOK,
	    taskqueue_thread_enqueue, /*context*/&xsoftc.xpt_taskq);

#ifdef CAM_BOOT_DELAY
	/*
	 * Override this value at compile time to assist our users
	 * who don't use loader to boot a kernel.
	 */
	xsoftc.boot_delay = CAM_BOOT_DELAY;
#endif
	/*
	 * The xpt layer is, itself, the equivalent of a SIM.
	 * Allow 16 ccbs in the ccb pool for it.  This should
	 * give decent parallelism when we probe buses and
	 * perform other XPT functions.
	 */
	devq = cam_simq_alloc(16);
	xpt_sim = cam_sim_alloc(xptaction,
				xptpoll,
				"xpt",
				/*softc*/NULL,
				/*unit*/0,
				/*mtx*/&xsoftc.xpt_lock,
				/*max_dev_transactions*/0,
				/*max_tagged_dev_transactions*/0,
				devq);
	if (xpt_sim == NULL)
		return (ENOMEM);

	mtx_lock(&xsoftc.xpt_lock);
	if ((status = xpt_bus_register(xpt_sim, NULL, 0)) != CAM_SUCCESS) {
		mtx_unlock(&xsoftc.xpt_lock);
		printf("xpt_init: xpt_bus_register failed with status %#x,"
		       " failing attach\n", status);
		return (EINVAL);
	}
	mtx_unlock(&xsoftc.xpt_lock);

	/*
	 * Looking at the XPT from the SIM layer, the XPT is
	 * the equivalent of a peripheral driver.  Allocate
	 * a peripheral driver entry for us.
	 */
	if ((status = xpt_create_path(&path, NULL, CAM_XPT_PATH_ID,
				      CAM_TARGET_WILDCARD,
				      CAM_LUN_WILDCARD)) != CAM_REQ_CMP) {
		printf("xpt_init: xpt_create_path failed with status %#x,"
		       " failing attach\n", status);
		return (EINVAL);
	}
	xpt_path_lock(path);
	cam_periph_alloc(xptregister, NULL, NULL, NULL, "xpt", CAM_PERIPH_BIO,
			 path, NULL, 0, xpt_sim);
	xpt_path_unlock(path);
	xpt_free_path(path);

	if (cam_num_doneqs < 1)
		cam_num_doneqs = 1 + mp_ncpus / 6;
	else if (cam_num_doneqs > MAXCPU)
		cam_num_doneqs = MAXCPU;
	for (i = 0; i < cam_num_doneqs; i++) {
		mtx_init(&cam_doneqs[i].cam_doneq_mtx, "CAM doneq", NULL,
		    MTX_DEF);
		STAILQ_INIT(&cam_doneqs[i].cam_doneq);
		error = kproc_kthread_add(xpt_done_td, &cam_doneqs[i],
		    &cam_proc, NULL, 0, 0, "cam", "doneq%d", i);
		if (error != 0) {
			cam_num_doneqs = i;
			break;
		}
	}
	if (cam_num_doneqs < 1) {
		printf("xpt_init: Cannot init completion queues "
		       "- failing attach\n");
		return (ENOMEM);
	}
	/*
	 * Register a callback for when interrupts are enabled.
	 */
	xsoftc.xpt_config_hook.ich_func = xpt_config;
	if (config_intrhook_establish(&xsoftc.xpt_config_hook) != 0) {
		printf("xpt_init: config_intrhook_establish failed "
		       "- failing attach\n");
	}

	return (0);
}

static cam_status
xptregister(struct cam_periph *periph, void *arg)
{
	struct cam_sim *xpt_sim;

	if (periph == NULL) {
		printf("xptregister: periph was NULL!!\n");
		return(CAM_REQ_CMP_ERR);
	}

	xpt_sim = (struct cam_sim *)arg;
	xpt_sim->softc = periph;
	xpt_periph = periph;
	periph->softc = NULL;

	return(CAM_REQ_CMP);
}

int32_t
xpt_add_periph(struct cam_periph *periph)
{
	struct cam_ed *device;
	int32_t	 status;

	TASK_INIT(&periph->periph_run_task, 0, xpt_run_allocq_task, periph);
	device = periph->path->device;
	status = CAM_REQ_CMP;
	if (device != NULL) {
		mtx_lock(&device->target->bus->eb_mtx);
		device->generation++;
		SLIST_INSERT_HEAD(&device->periphs, periph, periph_links);
		mtx_unlock(&device->target->bus->eb_mtx);
		atomic_add_32(&xsoftc.xpt_generation, 1);
	}

	return (status);
}

void
xpt_remove_periph(struct cam_periph *periph)
{
	struct cam_ed *device;

	device = periph->path->device;
	if (device != NULL) {
		mtx_lock(&device->target->bus->eb_mtx);
		device->generation++;
		SLIST_REMOVE(&device->periphs, periph, cam_periph, periph_links);
		mtx_unlock(&device->target->bus->eb_mtx);
		atomic_add_32(&xsoftc.xpt_generation, 1);
	}
}


void
xpt_announce_periph(struct cam_periph *periph, char *announce_string)
{
	struct	cam_path *path = periph->path;
	struct  xpt_proto *proto;

	cam_periph_assert(periph, MA_OWNED);
	periph->flags |= CAM_PERIPH_ANNOUNCED;

	printf("%s%d at %s%d bus %d scbus%d target %d lun %jx\n",
	       periph->periph_name, periph->unit_number,
	       path->bus->sim->sim_name,
	       path->bus->sim->unit_number,
	       path->bus->sim->bus_id,
	       path->bus->path_id,
	       path->target->target_id,
	       (uintmax_t)path->device->lun_id);
	printf("%s%d: ", periph->periph_name, periph->unit_number);
	proto = xpt_proto_find(path->device->protocol);
	if (proto)
		proto->ops->announce(path->device);
	else
		printf("%s%d: Unknown protocol device %d\n",
		    periph->periph_name, periph->unit_number,
		    path->device->protocol);
	if (path->device->serial_num_len > 0) {
		/* Don't wrap the screen  - print only the first 60 chars */
		printf("%s%d: Serial Number %.60s\n", periph->periph_name,
		       periph->unit_number, path->device->serial_num);
	}
	/* Announce transport details. */
	path->bus->xport->ops->announce(periph);
	/* Announce command queueing. */
	if (path->device->inq_flags & SID_CmdQue
	 || path->device->flags & CAM_DEV_TAG_AFTER_COUNT) {
		printf("%s%d: Command Queueing enabled\n",
		       periph->periph_name, periph->unit_number);
	}
	/* Announce caller's details if they've passed in. */
	if (announce_string != NULL)
		printf("%s%d: %s\n", periph->periph_name,
		       periph->unit_number, announce_string);
}

void
xpt_announce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb,
    char *announce_string)
{
	struct	cam_path *path = periph->path;
	struct  xpt_proto *proto;

	cam_periph_assert(periph, MA_OWNED);
	periph->flags |= CAM_PERIPH_ANNOUNCED;

	/* Fall back to the non-sbuf method if necessary */
	if (xsoftc.announce_nosbuf != 0) {
		xpt_announce_periph(periph, announce_string);
		return;
	}
	proto = xpt_proto_find(path->device->protocol);
	if (((proto != NULL) && (proto->ops->announce_sbuf == NULL)) ||
	    (path->bus->xport->ops->announce_sbuf == NULL)) {
		xpt_announce_periph(periph, announce_string);
		return;
	}

	sbuf_printf(sb, "%s%d at %s%d bus %d scbus%d target %d lun %jx\n",
	    periph->periph_name, periph->unit_number,
	    path->bus->sim->sim_name,
	    path->bus->sim->unit_number,
	    path->bus->sim->bus_id,
	    path->bus->path_id,
	    path->target->target_id,
	    (uintmax_t)path->device->lun_id);
	sbuf_printf(sb, "%s%d: ", periph->periph_name, periph->unit_number);

	if (proto)
		proto->ops->announce_sbuf(path->device, sb);
	else
		sbuf_printf(sb, "%s%d: Unknown protocol device %d\n",
		    periph->periph_name, periph->unit_number,
		    path->device->protocol);
	if (path->device->serial_num_len > 0) {
		/* Don't wrap the screen  - print only the first 60 chars */
		sbuf_printf(sb, "%s%d: Serial Number %.60s\n",
		    periph->periph_name, periph->unit_number,
		    path->device->serial_num);
	}
	/* Announce transport details. */
	path->bus->xport->ops->announce_sbuf(periph, sb);
	/* Announce command queueing. */
	if (path->device->inq_flags & SID_CmdQue
	 || path->device->flags & CAM_DEV_TAG_AFTER_COUNT) {
		sbuf_printf(sb, "%s%d: Command Queueing enabled\n",
		    periph->periph_name, periph->unit_number);
	}
	/* Announce caller's details if they've passed in. */
	if (announce_string != NULL)
		sbuf_printf(sb, "%s%d: %s\n", periph->periph_name,
		    periph->unit_number, announce_string);
}

void
xpt_announce_quirks(struct cam_periph *periph, int quirks, char *bit_string)
{
	if (quirks != 0) {
		printf("%s%d: quirks=0x%b\n", periph->periph_name,
		    periph->unit_number, quirks, bit_string);
	}
}

void
xpt_announce_quirks_sbuf(struct cam_periph *periph, struct sbuf *sb,
			 int quirks, char *bit_string)
{
	if (xsoftc.announce_nosbuf != 0) {
		xpt_announce_quirks(periph, quirks, bit_string);
		return;
	}

	if (quirks != 0) {
		sbuf_printf(sb, "%s%d: quirks=0x%b\n", periph->periph_name,
		    periph->unit_number, quirks, bit_string);
	}
}

void
xpt_denounce_periph(struct cam_periph *periph)
{
	struct	cam_path *path = periph->path;
	struct  xpt_proto *proto;

	cam_periph_assert(periph, MA_OWNED);
	printf("%s%d at %s%d bus %d scbus%d target %d lun %jx\n",
	       periph->periph_name, periph->unit_number,
	       path->bus->sim->sim_name,
	       path->bus->sim->unit_number,
	       path->bus->sim->bus_id,
	       path->bus->path_id,
	       path->target->target_id,
	       (uintmax_t)path->device->lun_id);
	printf("%s%d: ", periph->periph_name, periph->unit_number);
	proto = xpt_proto_find(path->device->protocol);
	if (proto)
		proto->ops->denounce(path->device);
	else
		printf("%s%d: Unknown protocol device %d\n",
		    periph->periph_name, periph->unit_number,
		    path->device->protocol);
	if (path->device->serial_num_len > 0)
		printf(" s/n %.60s", path->device->serial_num);
	printf(" detached\n");
}

void
xpt_denounce_periph_sbuf(struct cam_periph *periph, struct sbuf *sb)
{
	struct cam_path *path = periph->path;
	struct xpt_proto *proto;

	cam_periph_assert(periph, MA_OWNED);

	/* Fall back to the non-sbuf method if necessary */
	if (xsoftc.announce_nosbuf != 0) {
		xpt_denounce_periph(periph);
		return;
	}
	proto = xpt_proto_find(path->device->protocol);
	if ((proto != NULL) && (proto->ops->denounce_sbuf == NULL)) {
		xpt_denounce_periph(periph);
		return;
	}

	sbuf_printf(sb, "%s%d at %s%d bus %d scbus%d target %d lun %jx\n",
	    periph->periph_name, periph->unit_number,
	    path->bus->sim->sim_name,
	    path->bus->sim->unit_number,
	    path->bus->sim->bus_id,
	    path->bus->path_id,
	    path->target->target_id,
	    (uintmax_t)path->device->lun_id);
	sbuf_printf(sb, "%s%d: ", periph->periph_name, periph->unit_number);

	if (proto)
		proto->ops->denounce_sbuf(path->device, sb);
	else
		sbuf_printf(sb, "%s%d: Unknown protocol device %d\n",
		    periph->periph_name, periph->unit_number,
		    path->device->protocol);
	if (path->device->serial_num_len > 0)
		sbuf_printf(sb, " s/n %.60s", path->device->serial_num);
	sbuf_printf(sb, " detached\n");
}

int
xpt_getattr(char *buf, size_t len, const char *attr, struct cam_path *path)
{
	int ret = -1, l, o;
	struct ccb_dev_advinfo cdai;
	struct scsi_vpd_id_descriptor *idd;

	xpt_path_assert(path, MA_OWNED);

	memset(&cdai, 0, sizeof(cdai));
	xpt_setup_ccb(&cdai.ccb_h, path, CAM_PRIORITY_NORMAL);
	cdai.ccb_h.func_code = XPT_DEV_ADVINFO;
	cdai.flags = CDAI_FLAG_NONE;
	cdai.bufsiz = len;

	if (!strcmp(attr, "GEOM::ident"))
		cdai.buftype = CDAI_TYPE_SERIAL_NUM;
	else if (!strcmp(attr, "GEOM::physpath"))
		cdai.buftype = CDAI_TYPE_PHYS_PATH;
	else if (strcmp(attr, "GEOM::lunid") == 0 ||
		 strcmp(attr, "GEOM::lunname") == 0) {
		cdai.buftype = CDAI_TYPE_SCSI_DEVID;
		cdai.bufsiz = CAM_SCSI_DEVID_MAXLEN;
	} else
		goto out;

	cdai.buf = malloc(cdai.bufsiz, M_CAMXPT, M_NOWAIT|M_ZERO);
	if (cdai.buf == NULL) {
		ret = ENOMEM;
		goto out;
	}
	xpt_action((union ccb *)&cdai); /* can only be synchronous */
	if ((cdai.ccb_h.status & CAM_DEV_QFRZN) != 0)
		cam_release_devq(cdai.ccb_h.path, 0, 0, 0, FALSE);
	if (cdai.provsiz == 0)
		goto out;
	if (cdai.buftype == CDAI_TYPE_SCSI_DEVID) {
		if (strcmp(attr, "GEOM::lunid") == 0) {
			idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
			    cdai.provsiz, scsi_devid_is_lun_naa);
			if (idd == NULL)
				idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
				    cdai.provsiz, scsi_devid_is_lun_eui64);
			if (idd == NULL)
				idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
				    cdai.provsiz, scsi_devid_is_lun_uuid);
			if (idd == NULL)
				idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
				    cdai.provsiz, scsi_devid_is_lun_md5);
		} else
			idd = NULL;
		if (idd == NULL)
			idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
			    cdai.provsiz, scsi_devid_is_lun_t10);
		if (idd == NULL)
			idd = scsi_get_devid((struct scsi_vpd_device_id *)cdai.buf,
			    cdai.provsiz, scsi_devid_is_lun_name);
		if (idd == NULL)
			goto out;
		ret = 0;
		if ((idd->proto_codeset & SVPD_ID_CODESET_MASK) == SVPD_ID_CODESET_ASCII) {
			if (idd->length < len) {
				for (l = 0; l < idd->length; l++)
					buf[l] = idd->identifier[l] ?
					    idd->identifier[l] : ' ';
				buf[l] = 0;
			} else
				ret = EFAULT;
		} else if ((idd->proto_codeset & SVPD_ID_CODESET_MASK) == SVPD_ID_CODESET_UTF8) {
			l = strnlen(idd->identifier, idd->length);
			if (l < len) {
				bcopy(idd->identifier, buf, l);
				buf[l] = 0;
			} else
				ret = EFAULT;
		} else if ((idd->id_type & SVPD_ID_TYPE_MASK) == SVPD_ID_TYPE_UUID
		    && idd->identifier[0] == 0x10) {
			if ((idd->length - 2) * 2 + 4 < len) {
				for (l = 2, o = 0; l < idd->length; l++) {
					if (l == 6 || l == 8 || l == 10 || l == 12)
					    o += sprintf(buf + o, "-");
					o += sprintf(buf + o, "%02x",
					    idd->identifier[l]);
				}
			} else
				ret = EFAULT;
		} else {
			if (idd->length * 2 < len) {
				for (l = 0; l < idd->length; l++)
					sprintf(buf + l * 2, "%02x",
					    idd->identifier[l]);
			} else
				ret = EFAULT;
		}
	} else {
		ret = 0;
		if (strlcpy(buf, cdai.buf, len) >= len)
			ret = EFAULT;
	}

out:
	if (cdai.buf != NULL)
		free(cdai.buf, M_CAMXPT);
	return ret;
}

static dev_match_ret
xptbusmatch(struct dev_match_pattern *patterns, u_int num_patterns,
	    struct cam_eb *bus)
{
	dev_match_ret retval;
	u_int i;

	retval = DM_RET_NONE;

	/*
	 * If we aren't given something to match against, that's an error.
	 */
	if (bus == NULL)
		return(DM_RET_ERROR);

	/*
	 * If there are no match entries, then this bus matches no
	 * matter what.
	 */
	if ((patterns == NULL) || (num_patterns == 0))
		return(DM_RET_DESCEND | DM_RET_COPY);

	for (i = 0; i < num_patterns; i++) {
		struct bus_match_pattern *cur_pattern;

		/*
		 * If the pattern in question isn't for a bus node, we
		 * aren't interested.  However, we do indicate to the
		 * calling routine that we should continue descending the
		 * tree, since the user wants to match against lower-level
		 * EDT elements.
		 */
		if (patterns[i].type != DEV_MATCH_BUS) {
			if ((retval & DM_RET_ACTION_MASK) == DM_RET_NONE)
				retval |= DM_RET_DESCEND;
			continue;
		}

		cur_pattern = &patterns[i].pattern.bus_pattern;

		/*
		 * If they want to match any bus node, we give them any
		 * device node.
		 */
		if (cur_pattern->flags == BUS_MATCH_ANY) {
			/* set the copy flag */
			retval |= DM_RET_COPY;

			/*
			 * If we've already decided on an action, go ahead
			 * and return.
			 */
			if ((retval & DM_RET_ACTION_MASK) != DM_RET_NONE)
				return(retval);
		}

		/*
		 * Not sure why someone would do this...
		 */
		if (cur_pattern->flags == BUS_MATCH_NONE)
			continue;

		if (((cur_pattern->flags & BUS_MATCH_PATH) != 0)
		 && (cur_pattern->path_id != bus->path_id))
			continue;

		if (((cur_pattern->flags & BUS_MATCH_BUS_ID) != 0)
		 && (cur_pattern->bus_id != bus->sim->bus_id))
			continue;

		if (((cur_pattern->flags & BUS_MATCH_UNIT) != 0)
		 && (cur_pattern->unit_number != bus->sim->unit_number))
			continue;

		if (((cur_pattern->flags & BUS_MATCH_NAME) != 0)
		 && (strncmp(cur_pattern->dev_name, bus->sim->sim_name,
			     DEV_IDLEN) != 0))
			continue;

		/*
		 * If we get to this point, the user definitely wants
		 * information on this bus.  So tell the caller to copy the
		 * data out.
		 */
		retval |= DM_RET_COPY;

		/*
		 * If the return action has been set to descend, then we
		 * know that we've already seen a non-bus matching
		 * expression, therefore we need to further descend the tree.
		 * This won't change by continuing around the loop, so we
		 * go ahead and return.  If we haven't seen a non-bus
		 * matching expression, we keep going around the loop until
		 * we exhaust the matching expressions.  We'll set the stop
		 * flag once we fall out of the loop.
		 */
		if ((retval & DM_RET_ACTION_MASK) == DM_RET_DESCEND)
			return(retval);
	}

	/*
	 * If the return action hasn't been set to descend yet, that means
	 * we haven't seen anything other than bus matching patterns.  So
	 * tell the caller to stop descending the tree -- the user doesn't
	 * want to match against lower level tree elements.
	 */
	if ((retval & DM_RET_ACTION_MASK) == DM_RET_NONE)
		retval |= DM_RET_STOP;

	return(retval);
}

static dev_match_ret
xptdevicematch(struct dev_match_pattern *patterns, u_int num_patterns,
	       struct cam_ed *device)
{
	dev_match_ret retval;
	u_int i;

	retval = DM_RET_NONE;

	/*
	 * If we aren't given something to match against, that's an error.
	 */
	if (device == NULL)
		return(DM_RET_ERROR);

	/*
	 * If there are no match entries, then this device matches no
	 * matter what.
	 */
	if ((patterns == NULL) || (num_patterns == 0))
		return(DM_RET_DESCEND | DM_RET_COPY);

	for (i = 0; i < num_patterns; i++) {
		struct device_match_pattern *cur_pattern;
		struct scsi_vpd_device_id *device_id_page;

		/*
		 * If the pattern in question isn't for a device node, we
		 * aren't interested.
		 */
		if (patterns[i].type != DEV_MATCH_DEVICE) {
			if ((patterns[i].type == DEV_MATCH_PERIPH)
			 && ((retval & DM_RET_ACTION_MASK) == DM_RET_NONE))
				retval |= DM_RET_DESCEND;
			continue;
		}

		cur_pattern = &patterns[i].pattern.device_pattern;

		/* Error out if mutually exclusive options are specified. */
		if ((cur_pattern->flags & (DEV_MATCH_INQUIRY|DEV_MATCH_DEVID))
		 == (DEV_MATCH_INQUIRY|DEV_MATCH_DEVID))
			return(DM_RET_ERROR);

		/*
		 * If they want to match any device node, we give them any
		 * device node.
		 */
		if (cur_pattern->flags == DEV_MATCH_ANY)
			goto copy_dev_node;

		/*
		 * Not sure why someone would do this...
		 */
		if (cur_pattern->flags == DEV_MATCH_NONE)
			continue;

		if (((cur_pattern->flags & DEV_MATCH_PATH) != 0)
		 && (cur_pattern->path_id != device->target->bus->path_id))
			continue;

		if (((cur_pattern->flags & DEV_MATCH_TARGET) != 0)
		 && (cur_pattern->target_id != device->target->target_id))
			continue;

		if (((cur_pattern->flags & DEV_MATCH_LUN) != 0)
		 && (cur_pattern->target_lun != device->lun_id))
			continue;

		if (((cur_pattern->flags & DEV_MATCH_INQUIRY) != 0)
		 && (cam_quirkmatch((caddr_t)&device->inq_data,
				    (caddr_t)&cur_pattern->data.inq_pat,
				    1, sizeof(cur_pattern->data.inq_pat),
				    scsi_static_inquiry_match) == NULL))
			continue;

		device_id_page = (struct scsi_vpd_device_id *)device->device_id;
		if (((cur_pattern->flags & DEV_MATCH_DEVID) != 0)
		 && (device->device_id_len < SVPD_DEVICE_ID_HDR_LEN
		  || scsi_devid_match((uint8_t *)device_id_page->desc_list,
				      device->device_id_len
				    - SVPD_DEVICE_ID_HDR_LEN,
				      cur_pattern->data.devid_pat.id,
				      cur_pattern->data.devid_pat.id_len) != 0))
			continue;

copy_dev_node:
		/*
		 * If we get to this point, the user definitely wants
		 * information on this device.  So tell the caller to copy
		 * the data out.
		 */
		retval |= DM_RET_COPY;

		/*
		 * If the return action has been set to descend, then we
		 * know that we've already seen a peripheral matching
		 * expression, therefore we need to further descend the tree.
		 * This won't change by continuing around the loop, so we
		 * go ahead and return.  If we haven't seen a peripheral
		 * matching expression, we keep going around the loop until
		 * we exhaust the matching expressions.  We'll set the stop
		 * flag once we fall out of the loop.
		 */
		if ((retval & DM_RET_ACTION_MASK) == DM_RET_DESCEND)
			return(retval);
	}

	/*
	 * If the return action hasn't been set to descend yet, that means
	 * we haven't seen any peripheral matching patterns.  So tell the
	 * caller to stop descending the tree -- the user doesn't want to
	 * match against lower level tree elements.
	 */
	if ((retval & DM_RET_ACTION_MASK) == DM_RET_NONE)
		retval |= DM_RET_STOP;

	return(retval);
}

/*
 * Match a single peripheral against any number of match patterns.
 */
static dev_match_ret
xptperiphmatch(struct dev_match_pattern *patterns, u_int num_patterns,
	       struct cam_periph *periph)
{
	dev_match_ret retval;
	u_int i;

	/*
	 * If we aren't given something to match against, that's an error.
	 */
	if (periph == NULL)
		return(DM_RET_ERROR);

	/*
	 * If there are no match entries, then this peripheral matches no
	 * matter what.
	 */
	if ((patterns == NULL) || (num_patterns == 0))
		return(DM_RET_STOP | DM_RET_COPY);

	/*
	 * There aren't any nodes below a peripheral node, so there's no
	 * reason to descend the tree any further.
	 */
	retval = DM_RET_STOP;

	for (i = 0; i < num_patterns; i++) {
		struct periph_match_pattern *cur_pattern;

		/*
		 * If the pattern in question isn't for a peripheral, we
		 * aren't interested.
		 */
		if (patterns[i].type != DEV_MATCH_PERIPH)
			continue;

		cur_pattern = &patterns[i].pattern.periph_pattern;

		/*
		 * If they want to match on anything, then we will do so.
		 */
		if (cur_pattern->flags == PERIPH_MATCH_ANY) {
			/* set the copy flag */
			retval |= DM_RET_COPY;

			/*
			 * We've already set the return action to stop,
			 * since there are no nodes below peripherals in
			 * the tree.
			 */
			return(retval);
		}

		/*
		 * Not sure why someone would do this...
		 */
		if (cur_pattern->flags == PERIPH_MATCH_NONE)
			continue;

		if (((cur_pattern->flags & PERIPH_MATCH_PATH) != 0)
		 && (cur_pattern->path_id != periph->path->bus->path_id))
			continue;

		/*
		 * For the target and lun id's, we have to make sure the
		 * target and lun pointers aren't NULL.  The xpt peripheral
		 * has a wildcard target and device.
		 */
		if (((cur_pattern->flags & PERIPH_MATCH_TARGET) != 0)
		 && ((periph->path->target == NULL)
		 ||(cur_pattern->target_id != periph->path->target->target_id)))
			continue;

		if (((cur_pattern->flags & PERIPH_MATCH_LUN) != 0)
		 && ((periph->path->device == NULL)
		 || (cur_pattern->target_lun != periph->path->device->lun_id)))
			continue;

		if (((cur_pattern->flags & PERIPH_MATCH_UNIT) != 0)
		 && (cur_pattern->unit_number != periph->unit_number))
			continue;

		if (((cur_pattern->flags & PERIPH_MATCH_NAME) != 0)
		 && (strncmp(cur_pattern->periph_name, periph->periph_name,
			     DEV_IDLEN) != 0))
			continue;

		/*
		 * If we get to this point, the user definitely wants
		 * information on this peripheral.  So tell the caller to
		 * copy the data out.
		 */
		retval |= DM_RET_COPY;

		/*
		 * The return action has already been set to stop, since
		 * peripherals don't have any nodes below them in the EDT.
		 */
		return(retval);
	}

	/*
	 * If we get to this point, the peripheral that was passed in
	 * doesn't match any of the patterns.
	 */
	return(retval);
}

static int
xptedtbusfunc(struct cam_eb *bus, void *arg)
{
	struct ccb_dev_match *cdm;
	struct cam_et *target;
	dev_match_ret retval;

	cdm = (struct ccb_dev_match *)arg;

	/*
	 * If our position is for something deeper in the tree, that means
	 * that we've already seen this node.  So, we keep going down.
	 */
	if ((cdm->pos.position_type & CAM_DEV_POS_BUS)
	 && (cdm->pos.cookie.bus == bus)
	 && (cdm->pos.position_type & CAM_DEV_POS_TARGET)
	 && (cdm->pos.cookie.target != NULL))
		retval = DM_RET_DESCEND;
	else
		retval = xptbusmatch(cdm->patterns, cdm->num_patterns, bus);

	/*
	 * If we got an error, bail out of the search.
	 */
	if ((retval & DM_RET_ACTION_MASK) == DM_RET_ERROR) {
		cdm->status = CAM_DEV_MATCH_ERROR;
		return(0);
	}

	/*
	 * If the copy flag is set, copy this bus out.
	 */
	if (retval & DM_RET_COPY) {
		int spaceleft, j;

		spaceleft = cdm->match_buf_len - (cdm->num_matches *
			sizeof(struct dev_match_result));

		/*
		 * If we don't have enough space to put in another
		 * match result, save our position and tell the
		 * user there are more devices to check.
		 */
		if (spaceleft < sizeof(struct dev_match_result)) {
			bzero(&cdm->pos, sizeof(cdm->pos));
			cdm->pos.position_type =
				CAM_DEV_POS_EDT | CAM_DEV_POS_BUS;

			cdm->pos.cookie.bus = bus;
			cdm->pos.generations[CAM_BUS_GENERATION]=
				xsoftc.bus_generation;
			cdm->status = CAM_DEV_MATCH_MORE;
			return(0);
		}
		j = cdm->num_matches;
		cdm->num_matches++;
		cdm->matches[j].type = DEV_MATCH_BUS;
		cdm->matches[j].result.bus_result.path_id = bus->path_id;
		cdm->matches[j].result.bus_result.bus_id = bus->sim->bus_id;
		cdm->matches[j].result.bus_result.unit_number =
			bus->sim->unit_number;
		strlcpy(cdm->matches[j].result.bus_result.dev_name,
			bus->sim->sim_name,
			sizeof(cdm->matches[j].result.bus_result.dev_name));
	}

	/*
	 * If the user is only interested in buses, there's no
	 * reason to descend to the next level in the tree.
	 */
	if ((retval & DM_RET_ACTION_MASK) == DM_RET_STOP)
		return(1);

	/*
	 * If there is a target generation recorded, check it to
	 * make sure the target list hasn't changed.
	 */
	mtx_lock(&bus->eb_mtx);
	if ((cdm->pos.position_type & CAM_DEV_POS_BUS)
	 && (cdm->pos.cookie.bus == bus)
	 && (cdm->pos.position_type & CAM_DEV_POS_TARGET)
	 && (cdm->pos.cookie.target != NULL)) {
		if ((cdm->pos.generations[CAM_TARGET_GENERATION] !=
		    bus->generation)) {
			mtx_unlock(&bus->eb_mtx);
			cdm->status = CAM_DEV_MATCH_LIST_CHANGED;
			return (0);
		}
		target = (struct cam_et *)cdm->pos.cookie.target;
		target->refcount++;
	} else
		target = NULL;
	mtx_unlock(&bus->eb_mtx);

	return (xpttargettraverse(bus, target, xptedttargetfunc, arg));
}

static int
xptedttargetfunc(struct cam_et *target, void *arg)
{
	struct ccb_dev_match *cdm;
	struct cam_eb *bus;
	struct cam_ed *device;

	cdm = (struct ccb_dev_match *)arg;
	bus = target->bus;

	/*
	 * If there is a device list generation recorded, check it to
	 * make sure the device list hasn't changed.
	 */
	mtx_lock(&bus->eb_mtx);
	if ((cdm->pos.position_type & CAM_DEV_POS_BUS)
	 && (cdm->pos.cookie.bus == bus)
	 && (cdm->pos.position_type & CAM_DEV_POS_TARGET)
	 && (cdm->pos.cookie.target == target)
	 && (cdm->pos.position_type & CAM_DEV_POS_DEVICE)
	 && (cdm->pos.cookie.device != NULL)) {
		if (cdm->pos.generations[CAM_DEV_GENERATION] !=
		    target->generation) {
			mtx_unlock(&bus->eb_mtx);
			cdm->status = CAM_DEV_MATCH_LIST_CHANGED;
			return(0);
		}
		device = (struct cam_ed *)cdm->pos.cookie.device;
		device->refcount++;
	} else
		device = NULL;
	mtx_unlock(&bus->eb_mtx);

	return (xptdevicetraverse(target, device, xptedtdevicefunc, arg));
}

static int
xptedtdevicefunc(struct cam_ed *device, void *arg)
{
	struct cam_eb *bus;
	struct cam_periph *periph;
	struct ccb_dev_match *cdm;
	dev_match_ret retval;

	cdm = (struct ccb_dev_match *)arg;
	bus = device->target->bus;

	/*
	 * If our position is for something deeper in the tree, that means
	 * that we've already seen this node.  So, we keep going down.
	 */
	if ((cdm->pos.position_type & CAM_DEV_POS_DEVICE)
	 && (cdm->pos.cookie.device == device)
	 && (cdm->pos.position_type & CAM_DEV_POS_PERIPH)
	 && (cdm->pos.cookie.periph != NULL))
		retval = DM_RET_DESCEND;
	else
		retval = xptdevicematch(cdm->patterns, cdm->num_patterns,
					device);

	if ((retval & DM_RET_ACTION_MASK) == DM_RET_ERROR) {
		cdm->status = CAM_DEV_MATCH_ERROR;
		return(0);
	}

	/*
	 * If the copy flag is set, copy this device out.
	 */
	if (retval & DM_RET_COPY) {
		int spaceleft, j;

		spaceleft = cdm->match_buf_len - (cdm->num_matches *
			sizeof(struct dev_match_result));

		/*
		 * If we don't have enough space to put in another
		 * match result, save our position and tell the
		 * user there are more devices to check.
		 */
		if (spaceleft < sizeof(struct dev_match_result)) {
			bzero(&cdm->pos, sizeof(cdm->pos));
			cdm->pos.position_type =
				CAM_DEV_POS_EDT | CAM_DEV_POS_BUS |
				CAM_DEV_POS_TARGET | CAM_DEV_POS_DEVICE;

			cdm->pos.cookie.bus = device->target->bus;
			cdm->pos.generations[CAM_BUS_GENERATION]=
				xsoftc.bus_generation;
			cdm->pos.cookie.target = device->target;
			cdm->pos.generations[CAM_TARGET_GENERATION] =
				device->target->bus->generation;
			cdm->pos.cookie.device = device;
			cdm->pos.generations[CAM_DEV_GENERATION] =
				device->target->generation;
			cdm->status = CAM_DEV_MATCH_MORE;
			return(0);
		}
		j = cdm->num_matches;
		cdm->num_matches++;
		cdm->matches[j].type = DEV_MATCH_DEVICE;
		cdm->matches[j].result.device_result.path_id =
			device->target->bus->path_id;
		cdm->matches[j].result.device_result.target_id =
			device->target->target_id;
		cdm->matches[j].result.device_result.target_lun =
			device->lun_id;
		cdm->matches[j].result.device_result.protocol =
			device->protocol;
		bcopy(&device->inq_data,
		      &cdm->matches[j].result.device_result.inq_data,
		      sizeof(struct scsi_inquiry_data));
		bcopy(&device->ident_data,
		      &cdm->matches[j].result.device_result.ident_data,
		      sizeof(struct ata_params));

		/* Let the user know whether this device is unconfigured */
		if (device->flags & CAM_DEV_UNCONFIGURED)
			cdm->matches[j].result.device_result.flags =
				DEV_RESULT_UNCONFIGURED;
		else
			cdm->matches[j].result.device_result.flags =
				DEV_RESULT_NOFLAG;
	}

	/*
	 * If the user isn't interested in peripherals, don't descend
	 * the tree any further.
	 */
	if ((retval & DM_RET_ACTION_MASK) == DM_RET_STOP)
		return(1);

	/*
	 * If there is a peripheral list generation recorded, make sure
	 * it hasn't changed.
	 */
	xpt_lock_buses();
	mtx_lock(&bus->eb_mtx);
	if ((cdm->pos.position_type & CAM_DEV_POS_BUS)
	 && (cdm->pos.cookie.bus == bus)
	 && (cdm->pos.position_type & CAM_DEV_POS_TARGET)
	 && (cdm->pos.cookie.target == device->target)
	 && (cdm->pos.position_type & CAM_DEV_POS_DEVICE)
	 && (cdm->pos.cookie.device == device)
	 && (cdm->pos.position_type & CAM_DEV_POS_PERIPH)
	 && (cdm->pos.cookie.periph != NULL)) {
		if (cdm->pos.generations[CAM_PERIPH_GENERATION] !=
		    device->generation) {
			mtx_unlock(&bus->eb_mtx);
			xpt_unlock_buses();
			cdm->status = CAM_DEV_MATCH_LIST_CHANGED;
			return(0);
		}
		periph = (struct cam_periph *)cdm->pos.cookie.periph;
		periph->refcount++;
	} else
		periph = NULL;
	mtx_unlock(&bus->eb_mtx);
	xpt_unlock_buses();

	return (xptperiphtraverse(device, periph, xptedtperiphfunc, arg));
}

static int
xptedtperiphfunc(struct cam_periph *periph, void *arg)
{
	struct ccb_dev_match *cdm;
	dev_match_ret retval;

	cdm = (struct ccb_dev_match *)arg;

	retval = xptperiphmatch(cdm->patterns, cdm->num_patterns, periph);

	if ((retval & DM_RET_ACTION_MASK) == DM_RET_ERROR) {
		cdm->status = CAM_DEV_MATCH_ERROR;
		return(0);
	}

	/*
	 * If the copy flag is set, copy this peripheral out.
	 */
	if (retval & DM_RET_COPY) {
		int spaceleft, j;
		size_t l;

		spaceleft = cdm->match_buf_len - (cdm->num_matches *
			sizeof(struct dev_match_result));

		/*
		 * If we don't have enough space to put in another
		 * match result, save our position and tell the
		 * user there are more devices to check.
		 */
		if (spaceleft < sizeof(struct dev_match_result)) {
			bzero(&cdm->pos, sizeof(cdm->pos));
			cdm->pos.position_type =
				CAM_DEV_POS_EDT | CAM_DEV_POS_BUS |
				CAM_DEV_POS_TARGET | CAM_DEV_POS_DEVICE |
				CAM_DEV_POS_PERIPH;

			cdm->pos.cookie.bus = periph->path->bus;
			cdm->pos.generations[CAM_BUS_GENERATION]=
				xsoftc.bus_generation;
			cdm->pos.cookie.target = periph->path->target;
			cdm->pos.generations[CAM_TARGET_GENERATION] =
				periph->path->bus->generation;
			cdm->pos.cookie.device = periph->path->device;
			cdm->pos.generations[CAM_DEV_GENERATION] =
				periph->path->target->generation;
			cdm->pos.cookie.periph = periph;
			cdm->pos.generations[CAM_PERIPH_GENERATION] =
				periph->path->device->generation;
			cdm->status = CAM_DEV_MATCH_MORE;
			return(0);
		}

		j = cdm->num_matches;
		cdm->num_matches++;
		cdm->matches[j].type = DEV_MATCH_PERIPH;
		cdm->matches[j].result.periph_result.path_id =
			periph->path->bus->path_id;
		cdm->matches[j].result.periph_result.target_id =
			periph->path->target->target_id;
		cdm->matches[j].result.periph_result.target_lun =
			periph->path->device->lun_id;
		cdm->matches[j].result.periph_result.unit_number =
			periph->unit_number;
		l = sizeof(cdm->matches[j].result.periph_result.periph_name);
		strlcpy(cdm->matches[j].result.periph_result.periph_name,
			periph->periph_name, l);
	}

	return(1);
}

static int
xptedtmatch(struct ccb_dev_match *cdm)
{
	struct cam_eb *bus;
	int ret;

	cdm->num_matches = 0;

	/*
	 * Check the bus list generation.  If it has changed, the user
	 * needs to reset everything and start over.
	 */
	xpt_lock_buses();
	if ((cdm->pos.position_type & CAM_DEV_POS_BUS)
	 && (cdm->pos.cookie.bus != NULL)) {
		if (cdm->pos.generations[CAM_BUS_GENERATION] !=
		    xsoftc.bus_generation) {
			xpt_unlock_buses();
			cdm->status = CAM_DEV_MATCH_LIST_CHANGED;
			return(0);
		}
		bus = (struct cam_eb *)cdm->pos.cookie.bus;
		bus->refcount++;
	} else
		bus = NULL;
	xpt_unlock_buses();

	ret = xptbustraverse(bus, xptedtbusfunc, cdm);

	/*
	 * If we get back 0, that means that we had to stop before fully
	 * traversing the EDT.  It also means that one of the subroutines
	 * has set the status field to the proper value.  If we get back 1,
	 * we've fully traversed the EDT and copied out any matching entries.
	 */
	if (ret == 1)
		cdm->status = CAM_DEV_MATCH_LAST;

	return(ret);
}

static int
xptplistpdrvfunc(struct periph_driver **pdrv, void *arg)
{
	struct cam_periph *periph;
	struct ccb_dev_match *cdm;

	cdm = (struct ccb_dev_match *)arg;

	xpt_lock_buses();
	if ((cdm->pos.position_type & CAM_DEV_POS_PDPTR)
	 && (cdm->pos.cookie.pdrv == pdrv)
	 && (cdm->pos.position_type & CAM_DEV_POS_PERIPH)
	 && (cdm->pos.cookie.periph != NULL)) {
		if (cdm->pos.generations[CAM_PERIPH_GENERATION] !=
		    (*pdrv)->generation) {
			xpt_unlock_buses();
			cdm->status = CAM_DEV_MATCH_LIST_CHANGED;
			return(0);
		}
		periph = (struct cam_periph *)cdm->pos.cookie.periph;
		periph->refcount++;
	} else
		periph = NULL;
	xpt_unlock_buses();

	return (xptpdperiphtraverse(pdrv, periph, xptplistperiphfunc, arg));
}

static int
xptplistperiphfunc(struct cam_periph *periph, void *arg)
{
	struct ccb_dev_match *cdm;
	dev_match_ret retval;

	cdm = (struct ccb_dev_match *)arg;

	retval = xptperiphmatch(cdm->patterns, cdm->num_patterns, periph);

	if ((retval & DM_RET_ACTION_MASK) == DM_RET_ERROR) {
		cdm->status = CAM_DEV_MATCH_ERROR;
		return(0);
	}

	/*
	 * If the copy flag is set, copy this peripheral out.
	 */
	if (retval & DM_RET_COPY) {
		int spaceleft, j;
		size_t l;

		spaceleft = cdm->match_buf_len - (cdm->num_matches *
			sizeof(struct dev_match_result));

		/*
		 * If we don't have enough space to put in another
		 * match result, save our position and tell the
		 * user there are more devices to check.
		 */
		if (spaceleft < sizeof(struct dev_match_result)) {
			struct periph_driver **pdrv;

			pdrv = NULL;
			bzero(&cdm->pos, sizeof(cdm->pos));
			cdm->pos.position_type =
				CAM_DEV_POS_PDRV | CAM_DEV_POS_PDPTR |
				CAM_DEV_POS_PERIPH;

			/*
			 * This may look a bit non-sensical, but it is
			 * actually quite logical.  There are very few
			 * peripheral drivers, and bloating every peripheral
			 * structure with a pointer back to its parent
			 * peripheral driver linker set entry would cost
			 * more in the long run than doing this quick lookup.
			 */
			for (pdrv = periph_drivers; *pdrv != NULL; pdrv++) {
				if (strcmp((*pdrv)->driver_name,
				    periph->periph_name) == 0)
					break;
			}

			if (*pdrv == NULL) {
				cdm->status = CAM_DEV_MATCH_ERROR;
				return(0);
			}

			cdm->pos.cookie.pdrv = pdrv;
			/*
			 * The periph generation slot does double duty, as
			 * does the periph pointer slot.  They are used for
			 * both edt and pdrv lookups and positioning.
			 */
			cdm->pos.cookie.periph = periph;
			cdm->pos.generations[CAM_PERIPH_GENERATION] =
				(*pdrv)->generation;
			cdm->status = CAM_DEV_MATCH_MORE;
			return(0);
		}

		j = cdm->num_matches;
		cdm->num_matches++;
		cdm->matches[j].type = DEV_MATCH_PERIPH;
		cdm->matches[j].result.periph_result.path_id =
			periph->path->bus->path_id;

		/*
		 * The transport layer peripheral doesn't have a target or
		 * lun.
		 */
		if (periph->path->target)
			cdm->matches[j].result.periph_result.target_id =
				periph->path->target->target_id;
		else
			cdm->matches[j].result.periph_result.target_id =
				CAM_TARGET_WILDCARD;

		if (periph->path->device)
			cdm->matches[j].result.periph_result.target_lun =
				periph->path->device->lun_id;
		else
			cdm->matches[j].result.periph_result.target_lun =
				CAM_LUN_WILDCARD;

		cdm->matches[j].result.periph_result.unit_number =
			periph->unit_number;
		l = sizeof(cdm->matches[j].result.periph_result.periph_name);
		strlcpy(cdm->matches[j].result.periph_result.periph_name,
			periph->periph_name, l);
	}

	return(1);
}

static int
xptperiphlistmatch(struct ccb_dev_match *cdm)
{
	int ret;

	cdm->num_matches = 0;

	/*
	 * At this point in the edt traversal function, we check the bus
	 * list generation to make sure that no buses have been added or
	 * removed since the user last sent a XPT_DEV_MATCH ccb through.
	 * For the peripheral driver list traversal function, however, we
	 * don't have to worry about new peripheral driver types coming or
	 * going; they're in a linker set, and therefore can't change
	 * without a recompile.
	 */

	if ((cdm->pos.position_type & CAM_DEV_POS_PDPTR)
	 && (cdm->pos.cookie.pdrv != NULL))
		ret = xptpdrvtraverse(
				(struct periph_driver **)cdm->pos.cookie.pdrv,
				xptplistpdrvfunc, cdm);
	else
		ret = xptpdrvtraverse(NULL, xptplistpdrvfunc, cdm);

	/*
	 * If we get back 0, that means that we had to stop before fully
	 * traversing the peripheral driver tree.  It also means that one of
	 * the subroutines has set the status field to the proper value.  If
	 * we get back 1, we've fully traversed the EDT and copied out any
	 * matching entries.
	 */
	if (ret == 1)
		cdm->status = CAM_DEV_MATCH_LAST;

	return(ret);
}

static int
xptbustraverse(struct cam_eb *start_bus, xpt_busfunc_t *tr_func, void *arg)
{
	struct cam_eb *bus, *next_bus;
	int retval;

	retval = 1;
	if (start_bus)
		bus = start_bus;
	else {
		xpt_lock_buses();
		bus = TAILQ_FIRST(&xsoftc.xpt_busses);
		if (bus == NULL) {
			xpt_unlock_buses();
			return (retval);
		}
		bus->refcount++;
		xpt_unlock_buses();
	}
	for (; bus != NULL; bus = next_bus) {
		retval = tr_func(bus, arg);
		if (retval == 0) {
			xpt_release_bus(bus);
			break;
		}
		xpt_lock_buses();
		next_bus = TAILQ_NEXT(bus, links);
		if (next_bus)
			next_bus->refcount++;
		xpt_unlock_buses();
		xpt_release_bus(bus);
	}
	return(retval);
}

static int
xpttargettraverse(struct cam_eb *bus, struct cam_et *start_target,
		  xpt_targetfunc_t *tr_func, void *arg)
{
	struct cam_et *target, *next_target;
	int retval;

	retval = 1;
	if (start_target)
		target = start_target;
	else {
		mtx_lock(&bus->eb_mtx);
		target = TAILQ_FIRST(&bus->et_entries);
		if (target == NULL) {
			mtx_unlock(&bus->eb_mtx);
			return (retval);
		}
		target->refcount++;
		mtx_unlock(&bus->eb_mtx);
	}
	for (; target != NULL; target = next_target) {
		retval = tr_func(target, arg);
		if (retval == 0) {
			xpt_release_target(target);
			break;
		}
		mtx_lock(&bus->eb_mtx);
		next_target = TAILQ_NEXT(target, links);
		if (next_target)
			next_target->refcount++;
		mtx_unlock(&bus->eb_mtx);
		xpt_release_target(target);
	}
	return(retval);
}

static int
xptdevicetraverse(struct cam_et *target, struct cam_ed *start_device,
		  xpt_devicefunc_t *tr_func, void *arg)
{
	struct cam_eb *bus;
	struct cam_ed *device, *next_device;
	int retval;

	retval = 1;
	bus = target->bus;
	if (start_device)
		device = start_device;
	else {
		mtx_lock(&bus->eb_mtx);
		device = TAILQ_FIRST(&target->ed_entries);
		if (device == NULL) {
			mtx_unlock(&bus->eb_mtx);
			return (retval);
		}
		device->refcount++;
		mtx_unlock(&bus->eb_mtx);
	}
	for (; device != NULL; device = next_device) {
		mtx_lock(&device->device_mtx);
		retval = tr_func(device, arg);
		mtx_unlock(&device->device_mtx);
		if (retval == 0) {
			xpt_release_device(device);
			break;
		}
		mtx_lock(&bus->eb_mtx);
		next_device = TAILQ_NEXT(device, links);
		if (next_device)
			next_device->refcount++;
		mtx_unlock(&bus->eb_mtx);
		xpt_release_device(device);
	}
	return(retval);
}

static int
xptperiphtraverse(struct cam_ed *device, struct cam_periph *start_periph,
		  xpt_periphfunc_t *tr_func, void *arg)
{
	struct cam_eb *bus;
	struct cam_periph *periph, *next_periph;
	int retval;

	retval = 1;

	bus = device->target->bus;
	if (start_periph)
		periph = start_periph;
	else {
		xpt_lock_buses();
		mtx_lock(&bus->eb_mtx);
		periph = SLIST_FIRST(&device->periphs);
		while (periph != NULL && (periph->flags & CAM_PERIPH_FREE) != 0)
			periph = SLIST_NEXT(periph, periph_links);
		if (periph == NULL) {
			mtx_unlock(&bus->eb_mtx);
			xpt_unlock_buses();
			return (retval);
		}
		periph->refcount++;
		mtx_unlock(&bus->eb_mtx);
		xpt_unlock_buses();
	}
	for (; periph != NULL; periph = next_periph) {
		retval = tr_func(periph, arg);
		if (retval == 0) {
			cam_periph_release_locked(periph);
			break;
		}
		xpt_lock_buses();
		mtx_lock(&bus->eb_mtx);
		next_periph = SLIST_NEXT(periph, periph_links);
		while (next_periph != NULL &&
		    (next_periph->flags & CAM_PERIPH_FREE) != 0)
			next_periph = SLIST_NEXT(next_periph, periph_links);
		if (next_periph)
			next_periph->refcount++;
		mtx_unlock(&bus->eb_mtx);
		xpt_unlock_buses();
		cam_periph_release_locked(periph);
	}
	return(retval);
}

static int
xptpdrvtraverse(struct periph_driver **start_pdrv,
		xpt_pdrvfunc_t *tr_func, void *arg)
{
	struct periph_driver **pdrv;
	int retval;

	retval = 1;

	/*
	 * We don't traverse the peripheral driver list like we do the
	 * other lists, because it is a linker set, and therefore cannot be
	 * changed during runtime.  If the peripheral driver list is ever
	 * re-done to be something other than a linker set (i.e. it can
	 * change while the system is running), the list traversal should
	 * be modified to work like the other traversal functions.
	 */
	for (pdrv = (start_pdrv ? start_pdrv : periph_drivers);
	     *pdrv != NULL; pdrv++) {
		retval = tr_func(pdrv, arg);

		if (retval == 0)
			return(retval);
	}

	return(retval);
}

static int
xptpdperiphtraverse(struct periph_driver **pdrv,
		    struct cam_periph *start_periph,
		    xpt_periphfunc_t *tr_func, void *arg)
{
	struct cam_periph *periph, *next_periph;
	int retval;

	retval = 1;

	if (start_periph)
		periph = start_periph;
	else {
		xpt_lock_buses();
		periph = TAILQ_FIRST(&(*pdrv)->units);
		while (periph != NULL && (periph->flags & CAM_PERIPH_FREE) != 0)
			periph = TAILQ_NEXT(periph, unit_links);
		if (periph == NULL) {
			xpt_unlock_buses();
			return (retval);
		}
		periph->refcount++;
		xpt_unlock_buses();
	}
	for (; periph != NULL; periph = next_periph) {
		cam_periph_lock(periph);
		retval = tr_func(periph, arg);
		cam_periph_unlock(periph);
		if (retval == 0) {
			cam_periph_release(periph);
			break;
		}
		xpt_lock_buses();
		next_periph = TAILQ_NEXT(periph, unit_links);
		while (next_periph != NULL &&
		    (next_periph->flags & CAM_PERIPH_FREE) != 0)
			next_periph = TAILQ_NEXT(next_periph, unit_links);
		if (next_periph)
			next_periph->refcount++;
		xpt_unlock_buses();
		cam_periph_release(periph);
	}
	return(retval);
}

static int
xptdefbusfunc(struct cam_eb *bus, void *arg)
{
	struct xpt_traverse_config *tr_config;

	tr_config = (struct xpt_traverse_config *)arg;

	if (tr_config->depth == XPT_DEPTH_BUS) {
		xpt_busfunc_t *tr_func;

		tr_func = (xpt_busfunc_t *)tr_config->tr_func;

		return(tr_func(bus, tr_config->tr_arg));
	} else
		return(xpttargettraverse(bus, NULL, xptdeftargetfunc, arg));
}

static int
xptdeftargetfunc(struct cam_et *target, void *arg)
{
	struct xpt_traverse_config *tr_config;

	tr_config = (struct xpt_traverse_config *)arg;

	if (tr_config->depth == XPT_DEPTH_TARGET) {
		xpt_targetfunc_t *tr_func;

		tr_func = (xpt_targetfunc_t *)tr_config->tr_func;

		return(tr_func(target, tr_config->tr_arg));
	} else
		return(xptdevicetraverse(target, NULL, xptdefdevicefunc, arg));
}

static int
xptdefdevicefunc(struct cam_ed *device, void *arg)
{
	struct xpt_traverse_config *tr_config;

	tr_config = (struct xpt_traverse_config *)arg;

	if (tr_config->depth == XPT_DEPTH_DEVICE) {
		xpt_devicefunc_t *tr_func;

		tr_func = (xpt_devicefunc_t *)tr_config->tr_func;

		return(tr_func(device, tr_config->tr_arg));
	} else
		return(xptperiphtraverse(device, NULL, xptdefperiphfunc, arg));
}

static int
xptdefperiphfunc(struct cam_periph *periph, void *arg)
{
	struct xpt_traverse_config *tr_config;
	xpt_periphfunc_t *tr_func;

	tr_config = (struct xpt_traverse_config *)arg;

	tr_func = (xpt_periphfunc_t *)tr_config->tr_func;

	/*
	 * Unlike the other default functions, we don't check for depth
	 * here.  The peripheral driver level is the last level in the EDT,
	 * so if we're here, we should execute the function in question.
	 */
	return(tr_func(periph, tr_config->tr_arg));
}

/*
 * Execute the given function for every bus in the EDT.
 */
static int
xpt_for_all_busses(xpt_busfunc_t *tr_func, void *arg)
{
	struct xpt_traverse_config tr_config;

	tr_config.depth = XPT_DEPTH_BUS;
	tr_config.tr_func = tr_func;
	tr_config.tr_arg = arg;

	return(xptbustraverse(NULL, xptdefbusfunc, &tr_config));
}

/*
 * Execute the given function for every device in the EDT.
 */
static int
xpt_for_all_devices(xpt_devicefunc_t *tr_func, void *arg)
{
	struct xpt_traverse_config tr_config;

	tr_config.depth = XPT_DEPTH_DEVICE;
	tr_config.tr_func = tr_func;
	tr_config.tr_arg = arg;

	return(xptbustraverse(NULL, xptdefbusfunc, &tr_config));
}

static int
xptsetasyncfunc(struct cam_ed *device, void *arg)
{
	struct cam_path path;
	struct ccb_getdev cgd;
	struct ccb_setasync *csa = (struct ccb_setasync *)arg;

	/*
	 * Don't report unconfigured devices (Wildcard devs,
	 * devices only for target mode, device instances
	 * that have been invalidated but are waiting for
	 * their last reference count to be released).
	 */
	if ((device->flags & CAM_DEV_UNCONFIGURED) != 0)
		return (1);

	xpt_compile_path(&path,
			 NULL,
			 device->target->bus->path_id,
			 device->target->target_id,
			 device->lun_id);
	xpt_setup_ccb(&cgd.ccb_h, &path, CAM_PRIORITY_NORMAL);
	cgd.ccb_h.func_code = XPT_GDEV_TYPE;
	xpt_action((union ccb *)&cgd);
	csa->callback(csa->callback_arg,
			    AC_FOUND_DEVICE,
			    &path, &cgd);
	xpt_release_path(&path);

	return(1);
}

static int
xptsetasyncbusfunc(struct cam_eb *bus, void *arg)
{
	struct cam_path path;
	struct ccb_pathinq cpi;
	struct ccb_setasync *csa = (struct ccb_setasync *)arg;

	xpt_compile_path(&path, /*periph*/NULL,
			 bus->path_id,
			 CAM_TARGET_WILDCARD,
			 CAM_LUN_WILDCARD);
	xpt_path_lock(&path);
	xpt_path_inq(&cpi, &path);
	csa->callback(csa->callback_arg,
			    AC_PATH_REGISTERED,
			    &path, &cpi);
	xpt_path_unlock(&path);
	xpt_release_path(&path);

	return(1);
}

void
xpt_action(union ccb *start_ccb)
{

	CAM_DEBUG(start_ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_action: func %#x %s\n", start_ccb->ccb_h.func_code,
		xpt_action_name(start_ccb->ccb_h.func_code)));

	start_ccb->ccb_h.status = CAM_REQ_INPROG;
	(*(start_ccb->ccb_h.path->bus->xport->ops->action))(start_ccb);
}

void
xpt_action_default(union ccb *start_ccb)
{
	struct cam_path *path;
	struct cam_sim *sim;
	struct mtx *mtx;

	path = start_ccb->ccb_h.path;
	CAM_DEBUG(path, CAM_DEBUG_TRACE,
	    ("xpt_action_default: func %#x %s\n", start_ccb->ccb_h.func_code,
		xpt_action_name(start_ccb->ccb_h.func_code)));

	switch (start_ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
	{
		struct cam_ed *device;

		/*
		 * For the sake of compatibility with SCSI-1
		 * devices that may not understand the identify
		 * message, we include lun information in the
		 * second byte of all commands.  SCSI-1 specifies
		 * that luns are a 3 bit value and reserves only 3
		 * bits for lun information in the CDB.  Later
		 * revisions of the SCSI spec allow for more than 8
		 * luns, but have deprecated lun information in the
		 * CDB.  So, if the lun won't fit, we must omit.
		 *
		 * Also be aware that during initial probing for devices,
		 * the inquiry information is unknown but initialized to 0.
		 * This means that this code will be exercised while probing
		 * devices with an ANSI revision greater than 2.
		 */
		device = path->device;
		if (device->protocol_version <= SCSI_REV_2
		 && start_ccb->ccb_h.target_lun < 8
		 && (start_ccb->ccb_h.flags & CAM_CDB_POINTER) == 0) {

			start_ccb->csio.cdb_io.cdb_bytes[1] |=
			    start_ccb->ccb_h.target_lun << 5;
		}
		start_ccb->csio.scsi_status = SCSI_STATUS_OK;
	}
	/* FALLTHROUGH */
	case XPT_TARGET_IO:
	case XPT_CONT_TARGET_IO:
		start_ccb->csio.sense_resid = 0;
		start_ccb->csio.resid = 0;
		/* FALLTHROUGH */
	case XPT_ATA_IO:
		if (start_ccb->ccb_h.func_code == XPT_ATA_IO)
			start_ccb->ataio.resid = 0;
		/* FALLTHROUGH */
	case XPT_NVME_IO:
		/* FALLTHROUGH */
	case XPT_NVME_ADMIN:
		/* FALLTHROUGH */
	case XPT_MMC_IO:
		/* XXX just like nmve_io? */
	case XPT_RESET_DEV:
	case XPT_ENG_EXEC:
	case XPT_SMP_IO:
	{
		struct cam_devq *devq;

		devq = path->bus->sim->devq;
		mtx_lock(&devq->send_mtx);
		cam_ccbq_insert_ccb(&path->device->ccbq, start_ccb);
		if (xpt_schedule_devq(devq, path->device) != 0)
			xpt_run_devq(devq);
		mtx_unlock(&devq->send_mtx);
		break;
	}
	case XPT_CALC_GEOMETRY:
		/* Filter out garbage */
		if (start_ccb->ccg.block_size == 0
		 || start_ccb->ccg.volume_size == 0) {
			start_ccb->ccg.cylinders = 0;
			start_ccb->ccg.heads = 0;
			start_ccb->ccg.secs_per_track = 0;
			start_ccb->ccb_h.status = CAM_REQ_CMP;
			break;
		}
#if defined(__sparc64__)
		/*
		 * For sparc64, we may need adjust the geometry of large
		 * disks in order to fit the limitations of the 16-bit
		 * fields of the VTOC8 disk label.
		 */
		if (scsi_da_bios_params(&start_ccb->ccg) != 0) {
			start_ccb->ccb_h.status = CAM_REQ_CMP;
			break;
		}
#endif
		goto call_sim;
	case XPT_ABORT:
	{
		union ccb* abort_ccb;

		abort_ccb = start_ccb->cab.abort_ccb;
		if (XPT_FC_IS_DEV_QUEUED(abort_ccb)) {
			struct cam_ed *device;
			struct cam_devq *devq;

			device = abort_ccb->ccb_h.path->device;
			devq = device->sim->devq;

			mtx_lock(&devq->send_mtx);
			if (abort_ccb->ccb_h.pinfo.index > 0) {
				cam_ccbq_remove_ccb(&device->ccbq, abort_ccb);
				abort_ccb->ccb_h.status =
				    CAM_REQ_ABORTED|CAM_DEV_QFRZN;
				xpt_freeze_devq_device(device, 1);
				mtx_unlock(&devq->send_mtx);
				xpt_done(abort_ccb);
				start_ccb->ccb_h.status = CAM_REQ_CMP;
				break;
			}
			mtx_unlock(&devq->send_mtx);

			if (abort_ccb->ccb_h.pinfo.index == CAM_UNQUEUED_INDEX
			 && (abort_ccb->ccb_h.status & CAM_SIM_QUEUED) == 0) {
				/*
				 * We've caught this ccb en route to
				 * the SIM.  Flag it for abort and the
				 * SIM will do so just before starting
				 * real work on the CCB.
				 */
				abort_ccb->ccb_h.status =
				    CAM_REQ_ABORTED|CAM_DEV_QFRZN;
				xpt_freeze_devq(abort_ccb->ccb_h.path, 1);
				start_ccb->ccb_h.status = CAM_REQ_CMP;
				break;
			}
		}
		if (XPT_FC_IS_QUEUED(abort_ccb)
		 && (abort_ccb->ccb_h.pinfo.index == CAM_DONEQ_INDEX)) {
			/*
			 * It's already completed but waiting
			 * for our SWI to get to it.
			 */
			start_ccb->ccb_h.status = CAM_UA_ABORT;
			break;
		}
		/*
		 * If we weren't able to take care of the abort request
		 * in the XPT, pass the request down to the SIM for processing.
		 */
	}
	/* FALLTHROUGH */
	case XPT_ACCEPT_TARGET_IO:
	case XPT_EN_LUN:
	case XPT_IMMED_NOTIFY:
	case XPT_NOTIFY_ACK:
	case XPT_RESET_BUS:
	case XPT_IMMEDIATE_NOTIFY:
	case XPT_NOTIFY_ACKNOWLEDGE:
	case XPT_GET_SIM_KNOB_OLD:
	case XPT_GET_SIM_KNOB:
	case XPT_SET_SIM_KNOB:
	case XPT_GET_TRAN_SETTINGS:
	case XPT_SET_TRAN_SETTINGS:
	case XPT_PATH_INQ:
call_sim:
		sim = path->bus->sim;
		mtx = sim->mtx;
		if (mtx && !mtx_owned(mtx))
			mtx_lock(mtx);
		else
			mtx = NULL;

		CAM_DEBUG(path, CAM_DEBUG_TRACE,
		    ("Calling sim->sim_action(): func=%#x\n", start_ccb->ccb_h.func_code));
		(*(sim->sim_action))(sim, start_ccb);
		CAM_DEBUG(path, CAM_DEBUG_TRACE,
		    ("sim->sim_action returned: status=%#x\n", start_ccb->ccb_h.status));
		if (mtx)
			mtx_unlock(mtx);
		break;
	case XPT_PATH_STATS:
		start_ccb->cpis.last_reset = path->bus->last_reset;
		start_ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_GDEV_TYPE:
	{
		struct cam_ed *dev;

		dev = path->device;
		if ((dev->flags & CAM_DEV_UNCONFIGURED) != 0) {
			start_ccb->ccb_h.status = CAM_DEV_NOT_THERE;
		} else {
			struct ccb_getdev *cgd;

			cgd = &start_ccb->cgd;
			cgd->protocol = dev->protocol;
			cgd->inq_data = dev->inq_data;
			cgd->ident_data = dev->ident_data;
			cgd->inq_flags = dev->inq_flags;
			cgd->ccb_h.status = CAM_REQ_CMP;
			cgd->serial_num_len = dev->serial_num_len;
			if ((dev->serial_num_len > 0)
			 && (dev->serial_num != NULL))
				bcopy(dev->serial_num, cgd->serial_num,
				      dev->serial_num_len);
		}
		break;
	}
	case XPT_GDEV_STATS:
	{
		struct ccb_getdevstats *cgds = &start_ccb->cgds;
		struct cam_ed *dev = path->device;
		struct cam_eb *bus = path->bus;
		struct cam_et *tar = path->target;
		struct cam_devq *devq = bus->sim->devq;

		mtx_lock(&devq->send_mtx);
		cgds->dev_openings = dev->ccbq.dev_openings;
		cgds->dev_active = dev->ccbq.dev_active;
		cgds->allocated = dev->ccbq.allocated;
		cgds->queued = cam_ccbq_pending_ccb_count(&dev->ccbq);
		cgds->held = cgds->allocated - cgds->dev_active - cgds->queued;
		cgds->last_reset = tar->last_reset;
		cgds->maxtags = dev->maxtags;
		cgds->mintags = dev->mintags;
		if (timevalcmp(&tar->last_reset, &bus->last_reset, <))
			cgds->last_reset = bus->last_reset;
		mtx_unlock(&devq->send_mtx);
		cgds->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GDEVLIST:
	{
		struct cam_periph	*nperiph;
		struct periph_list	*periph_head;
		struct ccb_getdevlist	*cgdl;
		u_int			i;
		struct cam_ed		*device;
		int			found;


		found = 0;

		/*
		 * Don't want anyone mucking with our data.
		 */
		device = path->device;
		periph_head = &device->periphs;
		cgdl = &start_ccb->cgdl;

		/*
		 * Check and see if the list has changed since the user
		 * last requested a list member.  If so, tell them that the
		 * list has changed, and therefore they need to start over
		 * from the beginning.
		 */
		if ((cgdl->index != 0) &&
		    (cgdl->generation != device->generation)) {
			cgdl->status = CAM_GDEVLIST_LIST_CHANGED;
			break;
		}

		/*
		 * Traverse the list of peripherals and attempt to find
		 * the requested peripheral.
		 */
		for (nperiph = SLIST_FIRST(periph_head), i = 0;
		     (nperiph != NULL) && (i <= cgdl->index);
		     nperiph = SLIST_NEXT(nperiph, periph_links), i++) {
			if (i == cgdl->index) {
				strlcpy(cgdl->periph_name,
					nperiph->periph_name,
					sizeof(cgdl->periph_name));
				cgdl->unit_number = nperiph->unit_number;
				found = 1;
			}
		}
		if (found == 0) {
			cgdl->status = CAM_GDEVLIST_ERROR;
			break;
		}

		if (nperiph == NULL)
			cgdl->status = CAM_GDEVLIST_LAST_DEVICE;
		else
			cgdl->status = CAM_GDEVLIST_MORE_DEVS;

		cgdl->index++;
		cgdl->generation = device->generation;

		cgdl->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_DEV_MATCH:
	{
		dev_pos_type position_type;
		struct ccb_dev_match *cdm;

		cdm = &start_ccb->cdm;

		/*
		 * There are two ways of getting at information in the EDT.
		 * The first way is via the primary EDT tree.  It starts
		 * with a list of buses, then a list of targets on a bus,
		 * then devices/luns on a target, and then peripherals on a
		 * device/lun.  The "other" way is by the peripheral driver
		 * lists.  The peripheral driver lists are organized by
		 * peripheral driver.  (obviously)  So it makes sense to
		 * use the peripheral driver list if the user is looking
		 * for something like "da1", or all "da" devices.  If the
		 * user is looking for something on a particular bus/target
		 * or lun, it's generally better to go through the EDT tree.
		 */

		if (cdm->pos.position_type != CAM_DEV_POS_NONE)
			position_type = cdm->pos.position_type;
		else {
			u_int i;

			position_type = CAM_DEV_POS_NONE;

			for (i = 0; i < cdm->num_patterns; i++) {
				if ((cdm->patterns[i].type == DEV_MATCH_BUS)
				 ||(cdm->patterns[i].type == DEV_MATCH_DEVICE)){
					position_type = CAM_DEV_POS_EDT;
					break;
				}
			}

			if (cdm->num_patterns == 0)
				position_type = CAM_DEV_POS_EDT;
			else if (position_type == CAM_DEV_POS_NONE)
				position_type = CAM_DEV_POS_PDRV;
		}

		switch(position_type & CAM_DEV_POS_TYPEMASK) {
		case CAM_DEV_POS_EDT:
			xptedtmatch(cdm);
			break;
		case CAM_DEV_POS_PDRV:
			xptperiphlistmatch(cdm);
			break;
		default:
			cdm->status = CAM_DEV_MATCH_ERROR;
			break;
		}

		if (cdm->status == CAM_DEV_MATCH_ERROR)
			start_ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		else
			start_ccb->ccb_h.status = CAM_REQ_CMP;

		break;
	}
	case XPT_SASYNC_CB:
	{
		struct ccb_setasync *csa;
		struct async_node *cur_entry;
		struct async_list *async_head;
		u_int32_t added;

		csa = &start_ccb->csa;
		added = csa->event_enable;
		async_head = &path->device->asyncs;

		/*
		 * If there is already an entry for us, simply
		 * update it.
		 */
		cur_entry = SLIST_FIRST(async_head);
		while (cur_entry != NULL) {
			if ((cur_entry->callback_arg == csa->callback_arg)
			 && (cur_entry->callback == csa->callback))
				break;
			cur_entry = SLIST_NEXT(cur_entry, links);
		}

		if (cur_entry != NULL) {
		 	/*
			 * If the request has no flags set,
			 * remove the entry.
			 */
			added &= ~cur_entry->event_enable;
			if (csa->event_enable == 0) {
				SLIST_REMOVE(async_head, cur_entry,
					     async_node, links);
				xpt_release_device(path->device);
				free(cur_entry, M_CAMXPT);
			} else {
				cur_entry->event_enable = csa->event_enable;
			}
			csa->event_enable = added;
		} else {
			cur_entry = malloc(sizeof(*cur_entry), M_CAMXPT,
					   M_NOWAIT);
			if (cur_entry == NULL) {
				csa->ccb_h.status = CAM_RESRC_UNAVAIL;
				break;
			}
			cur_entry->event_enable = csa->event_enable;
			cur_entry->event_lock = (path->bus->sim->mtx &&
			    mtx_owned(path->bus->sim->mtx)) ? 1 : 0;
			cur_entry->callback_arg = csa->callback_arg;
			cur_entry->callback = csa->callback;
			SLIST_INSERT_HEAD(async_head, cur_entry, links);
			xpt_acquire_device(path->device);
		}
		start_ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_REL_SIMQ:
	{
		struct ccb_relsim *crs;
		struct cam_ed *dev;

		crs = &start_ccb->crs;
		dev = path->device;
		if (dev == NULL) {

			crs->ccb_h.status = CAM_DEV_NOT_THERE;
			break;
		}

		if ((crs->release_flags & RELSIM_ADJUST_OPENINGS) != 0) {

			/* Don't ever go below one opening */
			if (crs->openings > 0) {
				xpt_dev_ccbq_resize(path, crs->openings);
				if (bootverbose) {
					xpt_print(path,
					    "number of openings is now %d\n",
					    crs->openings);
				}
			}
		}

		mtx_lock(&dev->sim->devq->send_mtx);
		if ((crs->release_flags & RELSIM_RELEASE_AFTER_TIMEOUT) != 0) {

			if ((dev->flags & CAM_DEV_REL_TIMEOUT_PENDING) != 0) {

				/*
				 * Just extend the old timeout and decrement
				 * the freeze count so that a single timeout
				 * is sufficient for releasing the queue.
				 */
				start_ccb->ccb_h.flags &= ~CAM_DEV_QFREEZE;
				callout_stop(&dev->callout);
			} else {

				start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
			}

			callout_reset_sbt(&dev->callout,
			    SBT_1MS * crs->release_timeout, 0,
			    xpt_release_devq_timeout, dev, 0);

			dev->flags |= CAM_DEV_REL_TIMEOUT_PENDING;

		}

		if ((crs->release_flags & RELSIM_RELEASE_AFTER_CMDCMPLT) != 0) {

			if ((dev->flags & CAM_DEV_REL_ON_COMPLETE) != 0) {
				/*
				 * Decrement the freeze count so that a single
				 * completion is still sufficient to unfreeze
				 * the queue.
				 */
				start_ccb->ccb_h.flags &= ~CAM_DEV_QFREEZE;
			} else {

				dev->flags |= CAM_DEV_REL_ON_COMPLETE;
				start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
			}
		}

		if ((crs->release_flags & RELSIM_RELEASE_AFTER_QEMPTY) != 0) {

			if ((dev->flags & CAM_DEV_REL_ON_QUEUE_EMPTY) != 0
			 || (dev->ccbq.dev_active == 0)) {

				start_ccb->ccb_h.flags &= ~CAM_DEV_QFREEZE;
			} else {

				dev->flags |= CAM_DEV_REL_ON_QUEUE_EMPTY;
				start_ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
			}
		}
		mtx_unlock(&dev->sim->devq->send_mtx);

		if ((start_ccb->ccb_h.flags & CAM_DEV_QFREEZE) == 0)
			xpt_release_devq(path, /*count*/1, /*run_queue*/TRUE);
		start_ccb->crs.qfrozen_cnt = dev->ccbq.queue.qfrozen_cnt;
		start_ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_DEBUG: {
		struct cam_path *oldpath;

		/* Check that all request bits are supported. */
		if (start_ccb->cdbg.flags & ~(CAM_DEBUG_COMPILE)) {
			start_ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
			break;
		}

		cam_dflags = CAM_DEBUG_NONE;
		if (cam_dpath != NULL) {
			oldpath = cam_dpath;
			cam_dpath = NULL;
			xpt_free_path(oldpath);
		}
		if (start_ccb->cdbg.flags != CAM_DEBUG_NONE) {
			if (xpt_create_path(&cam_dpath, NULL,
					    start_ccb->ccb_h.path_id,
					    start_ccb->ccb_h.target_id,
					    start_ccb->ccb_h.target_lun) !=
					    CAM_REQ_CMP) {
				start_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
			} else {
				cam_dflags = start_ccb->cdbg.flags;
				start_ccb->ccb_h.status = CAM_REQ_CMP;
				xpt_print(cam_dpath, "debugging flags now %x\n",
				    cam_dflags);
			}
		} else
			start_ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_NOOP:
		if ((start_ccb->ccb_h.flags & CAM_DEV_QFREEZE) != 0)
			xpt_freeze_devq(path, 1);
		start_ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_REPROBE_LUN:
		xpt_async(AC_INQ_CHANGED, path, NULL);
		start_ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(start_ccb);
		break;
	default:
	case XPT_SDEV_TYPE:
	case XPT_TERM_IO:
	case XPT_ENG_INQ:
		/* XXX Implement */
		xpt_print(start_ccb->ccb_h.path,
		    "%s: CCB type %#x %s not supported\n", __func__,
		    start_ccb->ccb_h.func_code,
		    xpt_action_name(start_ccb->ccb_h.func_code));
		start_ccb->ccb_h.status = CAM_PROVIDE_FAIL;
		if (start_ccb->ccb_h.func_code & XPT_FC_DEV_QUEUED) {
			xpt_done(start_ccb);
		}
		break;
	}
	CAM_DEBUG(path, CAM_DEBUG_TRACE,
	    ("xpt_action_default: func= %#x %s status %#x\n",
		start_ccb->ccb_h.func_code,
 		xpt_action_name(start_ccb->ccb_h.func_code),
		start_ccb->ccb_h.status));
}

/*
 * Call the sim poll routine to allow the sim to complete
 * any inflight requests, then call camisr_runqueue to
 * complete any CCB that the polling completed.
 */
void
xpt_sim_poll(struct cam_sim *sim)
{
	struct mtx *mtx;

	mtx = sim->mtx;
	if (mtx)
		mtx_lock(mtx);
	(*(sim->sim_poll))(sim);
	if (mtx)
		mtx_unlock(mtx);
	camisr_runqueue();
}

uint32_t
xpt_poll_setup(union ccb *start_ccb)
{
	u_int32_t timeout;
	struct	  cam_sim *sim;
	struct	  cam_devq *devq;
	struct	  cam_ed *dev;

	timeout = start_ccb->ccb_h.timeout * 10;
	sim = start_ccb->ccb_h.path->bus->sim;
	devq = sim->devq;
	dev = start_ccb->ccb_h.path->device;

	/*
	 * Steal an opening so that no other queued requests
	 * can get it before us while we simulate interrupts.
	 */
	mtx_lock(&devq->send_mtx);
	dev->ccbq.dev_openings--;
	while((devq->send_openings <= 0 || dev->ccbq.dev_openings < 0) &&
	    (--timeout > 0)) {
		mtx_unlock(&devq->send_mtx);
		DELAY(100);
		xpt_sim_poll(sim);
		mtx_lock(&devq->send_mtx);
	}
	dev->ccbq.dev_openings++;
	mtx_unlock(&devq->send_mtx);

	return (timeout);
}

void
xpt_pollwait(union ccb *start_ccb, uint32_t timeout)
{

	while (--timeout > 0) {
		xpt_sim_poll(start_ccb->ccb_h.path->bus->sim);
		if ((start_ccb->ccb_h.status & CAM_STATUS_MASK)
		    != CAM_REQ_INPROG)
			break;
		DELAY(100);
	}

	if (timeout == 0) {
		/*
		 * XXX Is it worth adding a sim_timeout entry
		 * point so we can attempt recovery?  If
		 * this is only used for dumps, I don't think
		 * it is.
		 */
		start_ccb->ccb_h.status = CAM_CMD_TIMEOUT;
	}
}

void
xpt_polled_action(union ccb *start_ccb)
{
	uint32_t	timeout;
	struct cam_ed	*dev;

	timeout = start_ccb->ccb_h.timeout * 10;
	dev = start_ccb->ccb_h.path->device;

	mtx_unlock(&dev->device_mtx);

	timeout = xpt_poll_setup(start_ccb);
	if (timeout > 0) {
		xpt_action(start_ccb);
		xpt_pollwait(start_ccb, timeout);
	} else {
		start_ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
	}

	mtx_lock(&dev->device_mtx);
}

/*
 * Schedule a peripheral driver to receive a ccb when its
 * target device has space for more transactions.
 */
void
xpt_schedule(struct cam_periph *periph, u_int32_t new_priority)
{

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("xpt_schedule\n"));
	cam_periph_assert(periph, MA_OWNED);
	if (new_priority < periph->scheduled_priority) {
		periph->scheduled_priority = new_priority;
		xpt_run_allocq(periph, 0);
	}
}


/*
 * Schedule a device to run on a given queue.
 * If the device was inserted as a new entry on the queue,
 * return 1 meaning the device queue should be run. If we
 * were already queued, implying someone else has already
 * started the queue, return 0 so the caller doesn't attempt
 * to run the queue.
 */
static int
xpt_schedule_dev(struct camq *queue, cam_pinfo *pinfo,
		 u_int32_t new_priority)
{
	int retval;
	u_int32_t old_priority;

	CAM_DEBUG_PRINT(CAM_DEBUG_XPT, ("xpt_schedule_dev\n"));


	old_priority = pinfo->priority;

	/*
	 * Are we already queued?
	 */
	if (pinfo->index != CAM_UNQUEUED_INDEX) {
		/* Simply reorder based on new priority */
		if (new_priority < old_priority) {
			camq_change_priority(queue, pinfo->index,
					     new_priority);
			CAM_DEBUG_PRINT(CAM_DEBUG_XPT,
					("changed priority to %d\n",
					 new_priority));
			retval = 1;
		} else
			retval = 0;
	} else {
		/* New entry on the queue */
		if (new_priority < old_priority)
			pinfo->priority = new_priority;

		CAM_DEBUG_PRINT(CAM_DEBUG_XPT,
				("Inserting onto queue\n"));
		pinfo->generation = ++queue->generation;
		camq_insert(queue, pinfo);
		retval = 1;
	}
	return (retval);
}

static void
xpt_run_allocq_task(void *context, int pending)
{
	struct cam_periph *periph = context;

	cam_periph_lock(periph);
	periph->flags &= ~CAM_PERIPH_RUN_TASK;
	xpt_run_allocq(periph, 1);
	cam_periph_unlock(periph);
	cam_periph_release(periph);
}

static void
xpt_run_allocq(struct cam_periph *periph, int sleep)
{
	struct cam_ed	*device;
	union ccb	*ccb;
	uint32_t	 prio;

	cam_periph_assert(periph, MA_OWNED);
	if (periph->periph_allocating)
		return;
	cam_periph_doacquire(periph);
	periph->periph_allocating = 1;
	CAM_DEBUG_PRINT(CAM_DEBUG_XPT, ("xpt_run_allocq(%p)\n", periph));
	device = periph->path->device;
	ccb = NULL;
restart:
	while ((prio = min(periph->scheduled_priority,
	    periph->immediate_priority)) != CAM_PRIORITY_NONE &&
	    (periph->periph_allocated - (ccb != NULL ? 1 : 0) <
	     device->ccbq.total_openings || prio <= CAM_PRIORITY_OOB)) {

		if (ccb == NULL &&
		    (ccb = xpt_get_ccb_nowait(periph)) == NULL) {
			if (sleep) {
				ccb = xpt_get_ccb(periph);
				goto restart;
			}
			if (periph->flags & CAM_PERIPH_RUN_TASK)
				break;
			cam_periph_doacquire(periph);
			periph->flags |= CAM_PERIPH_RUN_TASK;
			taskqueue_enqueue(xsoftc.xpt_taskq,
			    &periph->periph_run_task);
			break;
		}
		xpt_setup_ccb(&ccb->ccb_h, periph->path, prio);
		if (prio == periph->immediate_priority) {
			periph->immediate_priority = CAM_PRIORITY_NONE;
			CAM_DEBUG_PRINT(CAM_DEBUG_XPT,
					("waking cam_periph_getccb()\n"));
			SLIST_INSERT_HEAD(&periph->ccb_list, &ccb->ccb_h,
					  periph_links.sle);
			wakeup(&periph->ccb_list);
		} else {
			periph->scheduled_priority = CAM_PRIORITY_NONE;
			CAM_DEBUG_PRINT(CAM_DEBUG_XPT,
					("calling periph_start()\n"));
			periph->periph_start(periph, ccb);
		}
		ccb = NULL;
	}
	if (ccb != NULL)
		xpt_release_ccb(ccb);
	periph->periph_allocating = 0;
	cam_periph_release_locked(periph);
}

static void
xpt_run_devq(struct cam_devq *devq)
{
	struct mtx *mtx;

	CAM_DEBUG_PRINT(CAM_DEBUG_XPT, ("xpt_run_devq\n"));

	devq->send_queue.qfrozen_cnt++;
	while ((devq->send_queue.entries > 0)
	    && (devq->send_openings > 0)
	    && (devq->send_queue.qfrozen_cnt <= 1)) {
		struct	cam_ed *device;
		union ccb *work_ccb;
		struct	cam_sim *sim;
		struct xpt_proto *proto;

		device = (struct cam_ed *)camq_remove(&devq->send_queue,
							   CAMQ_HEAD);
		CAM_DEBUG_PRINT(CAM_DEBUG_XPT,
				("running device %p\n", device));

		work_ccb = cam_ccbq_peek_ccb(&device->ccbq, CAMQ_HEAD);
		if (work_ccb == NULL) {
			printf("device on run queue with no ccbs???\n");
			continue;
		}

		if ((work_ccb->ccb_h.flags & CAM_HIGH_POWER) != 0) {

			mtx_lock(&xsoftc.xpt_highpower_lock);
		 	if (xsoftc.num_highpower <= 0) {
				/*
				 * We got a high power command, but we
				 * don't have any available slots.  Freeze
				 * the device queue until we have a slot
				 * available.
				 */
				xpt_freeze_devq_device(device, 1);
				STAILQ_INSERT_TAIL(&xsoftc.highpowerq, device,
						   highpowerq_entry);

				mtx_unlock(&xsoftc.xpt_highpower_lock);
				continue;
			} else {
				/*
				 * Consume a high power slot while
				 * this ccb runs.
				 */
				xsoftc.num_highpower--;
			}
			mtx_unlock(&xsoftc.xpt_highpower_lock);
		}
		cam_ccbq_remove_ccb(&device->ccbq, work_ccb);
		cam_ccbq_send_ccb(&device->ccbq, work_ccb);
		devq->send_openings--;
		devq->send_active++;
		xpt_schedule_devq(devq, device);
		mtx_unlock(&devq->send_mtx);

		if ((work_ccb->ccb_h.flags & CAM_DEV_QFREEZE) != 0) {
			/*
			 * The client wants to freeze the queue
			 * after this CCB is sent.
			 */
			xpt_freeze_devq(work_ccb->ccb_h.path, 1);
		}

		/* In Target mode, the peripheral driver knows best... */
		if (work_ccb->ccb_h.func_code == XPT_SCSI_IO) {
			if ((device->inq_flags & SID_CmdQue) != 0
			 && work_ccb->csio.tag_action != CAM_TAG_ACTION_NONE)
				work_ccb->ccb_h.flags |= CAM_TAG_ACTION_VALID;
			else
				/*
				 * Clear this in case of a retried CCB that
				 * failed due to a rejected tag.
				 */
				work_ccb->ccb_h.flags &= ~CAM_TAG_ACTION_VALID;
		}

		KASSERT(device == work_ccb->ccb_h.path->device,
		    ("device (%p) / path->device (%p) mismatch",
			device, work_ccb->ccb_h.path->device));
		proto = xpt_proto_find(device->protocol);
		if (proto && proto->ops->debug_out)
			proto->ops->debug_out(work_ccb);

		/*
		 * Device queues can be shared among multiple SIM instances
		 * that reside on different buses.  Use the SIM from the
		 * queued device, rather than the one from the calling bus.
		 */
		sim = device->sim;
		mtx = sim->mtx;
		if (mtx && !mtx_owned(mtx))
			mtx_lock(mtx);
		else
			mtx = NULL;
		work_ccb->ccb_h.qos.periph_data = cam_iosched_now();
		(*(sim->sim_action))(sim, work_ccb);
		if (mtx)
			mtx_unlock(mtx);
		mtx_lock(&devq->send_mtx);
	}
	devq->send_queue.qfrozen_cnt--;
}

/*
 * This function merges stuff from the slave ccb into the master ccb, while
 * keeping important fields in the master ccb constant.
 */
void
xpt_merge_ccb(union ccb *master_ccb, union ccb *slave_ccb)
{

	/*
	 * Pull fields that are valid for peripheral drivers to set
	 * into the master CCB along with the CCB "payload".
	 */
	master_ccb->ccb_h.retry_count = slave_ccb->ccb_h.retry_count;
	master_ccb->ccb_h.func_code = slave_ccb->ccb_h.func_code;
	master_ccb->ccb_h.timeout = slave_ccb->ccb_h.timeout;
	master_ccb->ccb_h.flags = slave_ccb->ccb_h.flags;
	bcopy(&(&slave_ccb->ccb_h)[1], &(&master_ccb->ccb_h)[1],
	      sizeof(union ccb) - sizeof(struct ccb_hdr));
}

void
xpt_setup_ccb_flags(struct ccb_hdr *ccb_h, struct cam_path *path,
		    u_int32_t priority, u_int32_t flags)
{

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_setup_ccb\n"));
	ccb_h->pinfo.priority = priority;
	ccb_h->path = path;
	ccb_h->path_id = path->bus->path_id;
	if (path->target)
		ccb_h->target_id = path->target->target_id;
	else
		ccb_h->target_id = CAM_TARGET_WILDCARD;
	if (path->device) {
		ccb_h->target_lun = path->device->lun_id;
		ccb_h->pinfo.generation = ++path->device->ccbq.queue.generation;
	} else {
		ccb_h->target_lun = CAM_TARGET_WILDCARD;
	}
	ccb_h->pinfo.index = CAM_UNQUEUED_INDEX;
	ccb_h->flags = flags;
	ccb_h->xflags = 0;
}

void
xpt_setup_ccb(struct ccb_hdr *ccb_h, struct cam_path *path, u_int32_t priority)
{
	xpt_setup_ccb_flags(ccb_h, path, priority, /*flags*/ 0);
}

/* Path manipulation functions */
cam_status
xpt_create_path(struct cam_path **new_path_ptr, struct cam_periph *perph,
		path_id_t path_id, target_id_t target_id, lun_id_t lun_id)
{
	struct	   cam_path *path;
	cam_status status;

	path = (struct cam_path *)malloc(sizeof(*path), M_CAMPATH, M_NOWAIT);

	if (path == NULL) {
		status = CAM_RESRC_UNAVAIL;
		return(status);
	}
	status = xpt_compile_path(path, perph, path_id, target_id, lun_id);
	if (status != CAM_REQ_CMP) {
		free(path, M_CAMPATH);
		path = NULL;
	}
	*new_path_ptr = path;
	return (status);
}

cam_status
xpt_create_path_unlocked(struct cam_path **new_path_ptr,
			 struct cam_periph *periph, path_id_t path_id,
			 target_id_t target_id, lun_id_t lun_id)
{

	return (xpt_create_path(new_path_ptr, periph, path_id, target_id,
	    lun_id));
}

cam_status
xpt_compile_path(struct cam_path *new_path, struct cam_periph *perph,
		 path_id_t path_id, target_id_t target_id, lun_id_t lun_id)
{
	struct	     cam_eb *bus;
	struct	     cam_et *target;
	struct	     cam_ed *device;
	cam_status   status;

	status = CAM_REQ_CMP;	/* Completed without error */
	target = NULL;		/* Wildcarded */
	device = NULL;		/* Wildcarded */

	/*
	 * We will potentially modify the EDT, so block interrupts
	 * that may attempt to create cam paths.
	 */
	bus = xpt_find_bus(path_id);
	if (bus == NULL) {
		status = CAM_PATH_INVALID;
	} else {
		xpt_lock_buses();
		mtx_lock(&bus->eb_mtx);
		target = xpt_find_target(bus, target_id);
		if (target == NULL) {
			/* Create one */
			struct cam_et *new_target;

			new_target = xpt_alloc_target(bus, target_id);
			if (new_target == NULL) {
				status = CAM_RESRC_UNAVAIL;
			} else {
				target = new_target;
			}
		}
		xpt_unlock_buses();
		if (target != NULL) {
			device = xpt_find_device(target, lun_id);
			if (device == NULL) {
				/* Create one */
				struct cam_ed *new_device;

				new_device =
				    (*(bus->xport->ops->alloc_device))(bus,
								       target,
								       lun_id);
				if (new_device == NULL) {
					status = CAM_RESRC_UNAVAIL;
				} else {
					device = new_device;
				}
			}
		}
		mtx_unlock(&bus->eb_mtx);
	}

	/*
	 * Only touch the user's data if we are successful.
	 */
	if (status == CAM_REQ_CMP) {
		new_path->periph = perph;
		new_path->bus = bus;
		new_path->target = target;
		new_path->device = device;
		CAM_DEBUG(new_path, CAM_DEBUG_TRACE, ("xpt_compile_path\n"));
	} else {
		if (device != NULL)
			xpt_release_device(device);
		if (target != NULL)
			xpt_release_target(target);
		if (bus != NULL)
			xpt_release_bus(bus);
	}
	return (status);
}

cam_status
xpt_clone_path(struct cam_path **new_path_ptr, struct cam_path *path)
{
	struct	   cam_path *new_path;

	new_path = (struct cam_path *)malloc(sizeof(*path), M_CAMPATH, M_NOWAIT);
	if (new_path == NULL)
		return(CAM_RESRC_UNAVAIL);
	xpt_copy_path(new_path, path);
	*new_path_ptr = new_path;
	return (CAM_REQ_CMP);
}

void
xpt_copy_path(struct cam_path *new_path, struct cam_path *path)
{

	*new_path = *path;
	if (path->bus != NULL)
		xpt_acquire_bus(path->bus);
	if (path->target != NULL)
		xpt_acquire_target(path->target);
	if (path->device != NULL)
		xpt_acquire_device(path->device);
}

void
xpt_release_path(struct cam_path *path)
{
	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_release_path\n"));
	if (path->device != NULL) {
		xpt_release_device(path->device);
		path->device = NULL;
	}
	if (path->target != NULL) {
		xpt_release_target(path->target);
		path->target = NULL;
	}
	if (path->bus != NULL) {
		xpt_release_bus(path->bus);
		path->bus = NULL;
	}
}

void
xpt_free_path(struct cam_path *path)
{

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_free_path\n"));
	xpt_release_path(path);
	free(path, M_CAMPATH);
}

void
xpt_path_counts(struct cam_path *path, uint32_t *bus_ref,
    uint32_t *periph_ref, uint32_t *target_ref, uint32_t *device_ref)
{

	xpt_lock_buses();
	if (bus_ref) {
		if (path->bus)
			*bus_ref = path->bus->refcount;
		else
			*bus_ref = 0;
	}
	if (periph_ref) {
		if (path->periph)
			*periph_ref = path->periph->refcount;
		else
			*periph_ref = 0;
	}
	xpt_unlock_buses();
	if (target_ref) {
		if (path->target)
			*target_ref = path->target->refcount;
		else
			*target_ref = 0;
	}
	if (device_ref) {
		if (path->device)
			*device_ref = path->device->refcount;
		else
			*device_ref = 0;
	}
}

/*
 * Return -1 for failure, 0 for exact match, 1 for match with wildcards
 * in path1, 2 for match with wildcards in path2.
 */
int
xpt_path_comp(struct cam_path *path1, struct cam_path *path2)
{
	int retval = 0;

	if (path1->bus != path2->bus) {
		if (path1->bus->path_id == CAM_BUS_WILDCARD)
			retval = 1;
		else if (path2->bus->path_id == CAM_BUS_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	if (path1->target != path2->target) {
		if (path1->target->target_id == CAM_TARGET_WILDCARD) {
			if (retval == 0)
				retval = 1;
		} else if (path2->target->target_id == CAM_TARGET_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	if (path1->device != path2->device) {
		if (path1->device->lun_id == CAM_LUN_WILDCARD) {
			if (retval == 0)
				retval = 1;
		} else if (path2->device->lun_id == CAM_LUN_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	return (retval);
}

int
xpt_path_comp_dev(struct cam_path *path, struct cam_ed *dev)
{
	int retval = 0;

	if (path->bus != dev->target->bus) {
		if (path->bus->path_id == CAM_BUS_WILDCARD)
			retval = 1;
		else if (dev->target->bus->path_id == CAM_BUS_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	if (path->target != dev->target) {
		if (path->target->target_id == CAM_TARGET_WILDCARD) {
			if (retval == 0)
				retval = 1;
		} else if (dev->target->target_id == CAM_TARGET_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	if (path->device != dev) {
		if (path->device->lun_id == CAM_LUN_WILDCARD) {
			if (retval == 0)
				retval = 1;
		} else if (dev->lun_id == CAM_LUN_WILDCARD)
			retval = 2;
		else
			return (-1);
	}
	return (retval);
}

void
xpt_print_path(struct cam_path *path)
{
	struct sbuf sb;
	char buffer[XPT_PRINT_LEN];

	sbuf_new(&sb, buffer, XPT_PRINT_LEN, SBUF_FIXEDLEN);
	xpt_path_sbuf(path, &sb);
	sbuf_finish(&sb);
	printf("%s", sbuf_data(&sb));
	sbuf_delete(&sb);
}

void
xpt_print_device(struct cam_ed *device)
{

	if (device == NULL)
		printf("(nopath): ");
	else {
		printf("(noperiph:%s%d:%d:%d:%jx): ", device->sim->sim_name,
		       device->sim->unit_number,
		       device->sim->bus_id,
		       device->target->target_id,
		       (uintmax_t)device->lun_id);
	}
}

void
xpt_print(struct cam_path *path, const char *fmt, ...)
{
	va_list ap;
	struct sbuf sb;
	char buffer[XPT_PRINT_LEN];

	sbuf_new(&sb, buffer, XPT_PRINT_LEN, SBUF_FIXEDLEN);

	xpt_path_sbuf(path, &sb);
	va_start(ap, fmt);
	sbuf_vprintf(&sb, fmt, ap);
	va_end(ap);

	sbuf_finish(&sb);
	printf("%s", sbuf_data(&sb));
	sbuf_delete(&sb);
}

int
xpt_path_string(struct cam_path *path, char *str, size_t str_len)
{
	struct sbuf sb;
	int len;

	sbuf_new(&sb, str, str_len, 0);
	len = xpt_path_sbuf(path, &sb);
	sbuf_finish(&sb);
	return (len);
}

int
xpt_path_sbuf(struct cam_path *path, struct sbuf *sb)
{

	if (path == NULL)
		sbuf_printf(sb, "(nopath): ");
	else {
		if (path->periph != NULL)
			sbuf_printf(sb, "(%s%d:", path->periph->periph_name,
				    path->periph->unit_number);
		else
			sbuf_printf(sb, "(noperiph:");

		if (path->bus != NULL)
			sbuf_printf(sb, "%s%d:%d:", path->bus->sim->sim_name,
				    path->bus->sim->unit_number,
				    path->bus->sim->bus_id);
		else
			sbuf_printf(sb, "nobus:");

		if (path->target != NULL)
			sbuf_printf(sb, "%d:", path->target->target_id);
		else
			sbuf_printf(sb, "X:");

		if (path->device != NULL)
			sbuf_printf(sb, "%jx): ",
			    (uintmax_t)path->device->lun_id);
		else
			sbuf_printf(sb, "X): ");
	}

	return(sbuf_len(sb));
}

path_id_t
xpt_path_path_id(struct cam_path *path)
{
	return(path->bus->path_id);
}

target_id_t
xpt_path_target_id(struct cam_path *path)
{
	if (path->target != NULL)
		return (path->target->target_id);
	else
		return (CAM_TARGET_WILDCARD);
}

lun_id_t
xpt_path_lun_id(struct cam_path *path)
{
	if (path->device != NULL)
		return (path->device->lun_id);
	else
		return (CAM_LUN_WILDCARD);
}

struct cam_sim *
xpt_path_sim(struct cam_path *path)
{

	return (path->bus->sim);
}

struct cam_periph*
xpt_path_periph(struct cam_path *path)
{

	return (path->periph);
}

/*
 * Release a CAM control block for the caller.  Remit the cost of the structure
 * to the device referenced by the path.  If the this device had no 'credits'
 * and peripheral drivers have registered async callbacks for this notification
 * call them now.
 */
void
xpt_release_ccb(union ccb *free_ccb)
{
	struct	 cam_ed *device;
	struct	 cam_periph *periph;

	CAM_DEBUG_PRINT(CAM_DEBUG_XPT, ("xpt_release_ccb\n"));
	xpt_path_assert(free_ccb->ccb_h.path, MA_OWNED);
	device = free_ccb->ccb_h.path->device;
	periph = free_ccb->ccb_h.path->periph;

	xpt_free_ccb(free_ccb);
	periph->periph_allocated--;
	cam_ccbq_release_opening(&device->ccbq);
	xpt_run_allocq(periph, 0);
}

/* Functions accessed by SIM drivers */

static struct xpt_xport_ops xport_default_ops = {
	.alloc_device = xpt_alloc_device_default,
	.action = xpt_action_default,
	.async = xpt_dev_async_default,
};
static struct xpt_xport xport_default = {
	.xport = XPORT_UNKNOWN,
	.name = "unknown",
	.ops = &xport_default_ops,
};

CAM_XPT_XPORT(xport_default);

/*
 * A sim structure, listing the SIM entry points and instance
 * identification info is passed to xpt_bus_register to hook the SIM
 * into the CAM framework.  xpt_bus_register creates a cam_eb entry
 * for this new bus and places it in the array of buses and assigns
 * it a path_id.  The path_id may be influenced by "hard wiring"
 * information specified by the user.  Once interrupt services are
 * available, the bus will be probed.
 */
int32_t
xpt_bus_register(struct cam_sim *sim, device_t parent, u_int32_t bus)
{
	struct cam_eb *new_bus;
	struct cam_eb *old_bus;
	struct ccb_pathinq cpi;
	struct cam_path *path;
	cam_status status;

	sim->bus_id = bus;
	new_bus = (struct cam_eb *)malloc(sizeof(*new_bus),
					  M_CAMXPT, M_NOWAIT|M_ZERO);
	if (new_bus == NULL) {
		/* Couldn't satisfy request */
		return (CAM_RESRC_UNAVAIL);
	}

	mtx_init(&new_bus->eb_mtx, "CAM bus lock", NULL, MTX_DEF);
	TAILQ_INIT(&new_bus->et_entries);
	cam_sim_hold(sim);
	new_bus->sim = sim;
	timevalclear(&new_bus->last_reset);
	new_bus->flags = 0;
	new_bus->refcount = 1;	/* Held until a bus_deregister event */
	new_bus->generation = 0;

	xpt_lock_buses();
	sim->path_id = new_bus->path_id =
	    xptpathid(sim->sim_name, sim->unit_number, sim->bus_id);
	old_bus = TAILQ_FIRST(&xsoftc.xpt_busses);
	while (old_bus != NULL
	    && old_bus->path_id < new_bus->path_id)
		old_bus = TAILQ_NEXT(old_bus, links);
	if (old_bus != NULL)
		TAILQ_INSERT_BEFORE(old_bus, new_bus, links);
	else
		TAILQ_INSERT_TAIL(&xsoftc.xpt_busses, new_bus, links);
	xsoftc.bus_generation++;
	xpt_unlock_buses();

	/*
	 * Set a default transport so that a PATH_INQ can be issued to
	 * the SIM.  This will then allow for probing and attaching of
	 * a more appropriate transport.
	 */
	new_bus->xport = &xport_default;

	status = xpt_create_path(&path, /*periph*/NULL, sim->path_id,
				  CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP) {
		xpt_release_bus(new_bus);
		return (CAM_RESRC_UNAVAIL);
	}

	xpt_path_inq(&cpi, path);

	if (cpi.ccb_h.status == CAM_REQ_CMP) {
		struct xpt_xport **xpt;

		SET_FOREACH(xpt, cam_xpt_xport_set) {
			if ((*xpt)->xport == cpi.transport) {
				new_bus->xport = *xpt;
				break;
			}
		}
		if (new_bus->xport == NULL) {
			xpt_print(path,
			    "No transport found for %d\n", cpi.transport);
			xpt_release_bus(new_bus);
			free(path, M_CAMXPT);
			return (CAM_RESRC_UNAVAIL);
		}
	}

	/* Notify interested parties */
	if (sim->path_id != CAM_XPT_PATH_ID) {

		xpt_async(AC_PATH_REGISTERED, path, &cpi);
		if ((cpi.hba_misc & PIM_NOSCAN) == 0) {
			union	ccb *scan_ccb;

			/* Initiate bus rescan. */
			scan_ccb = xpt_alloc_ccb_nowait();
			if (scan_ccb != NULL) {
				scan_ccb->ccb_h.path = path;
				scan_ccb->ccb_h.func_code = XPT_SCAN_BUS;
				scan_ccb->crcn.flags = 0;
				xpt_rescan(scan_ccb);
			} else {
				xpt_print(path,
					  "Can't allocate CCB to scan bus\n");
				xpt_free_path(path);
			}
		} else
			xpt_free_path(path);
	} else
		xpt_free_path(path);
	return (CAM_SUCCESS);
}

int32_t
xpt_bus_deregister(path_id_t pathid)
{
	struct cam_path bus_path;
	cam_status status;

	status = xpt_compile_path(&bus_path, NULL, pathid,
				  CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
	if (status != CAM_REQ_CMP)
		return (status);

	xpt_async(AC_LOST_DEVICE, &bus_path, NULL);
	xpt_async(AC_PATH_DEREGISTERED, &bus_path, NULL);

	/* Release the reference count held while registered. */
	xpt_release_bus(bus_path.bus);
	xpt_release_path(&bus_path);

	return (CAM_REQ_CMP);
}

static path_id_t
xptnextfreepathid(void)
{
	struct cam_eb *bus;
	path_id_t pathid;
	const char *strval;

	mtx_assert(&xsoftc.xpt_topo_lock, MA_OWNED);
	pathid = 0;
	bus = TAILQ_FIRST(&xsoftc.xpt_busses);
retry:
	/* Find an unoccupied pathid */
	while (bus != NULL && bus->path_id <= pathid) {
		if (bus->path_id == pathid)
			pathid++;
		bus = TAILQ_NEXT(bus, links);
	}

	/*
	 * Ensure that this pathid is not reserved for
	 * a bus that may be registered in the future.
	 */
	if (resource_string_value("scbus", pathid, "at", &strval) == 0) {
		++pathid;
		/* Start the search over */
		goto retry;
	}
	return (pathid);
}

static path_id_t
xptpathid(const char *sim_name, int sim_unit, int sim_bus)
{
	path_id_t pathid;
	int i, dunit, val;
	char buf[32];
	const char *dname;

	pathid = CAM_XPT_PATH_ID;
	snprintf(buf, sizeof(buf), "%s%d", sim_name, sim_unit);
	if (strcmp(buf, "xpt0") == 0 && sim_bus == 0)
		return (pathid);
	i = 0;
	while ((resource_find_match(&i, &dname, &dunit, "at", buf)) == 0) {
		if (strcmp(dname, "scbus")) {
			/* Avoid a bit of foot shooting. */
			continue;
		}
		if (dunit < 0)		/* unwired?! */
			continue;
		if (resource_int_value("scbus", dunit, "bus", &val) == 0) {
			if (sim_bus == val) {
				pathid = dunit;
				break;
			}
		} else if (sim_bus == 0) {
			/* Unspecified matches bus 0 */
			pathid = dunit;
			break;
		} else {
			printf("Ambiguous scbus configuration for %s%d "
			       "bus %d, cannot wire down.  The kernel "
			       "config entry for scbus%d should "
			       "specify a controller bus.\n"
			       "Scbus will be assigned dynamically.\n",
			       sim_name, sim_unit, sim_bus, dunit);
			break;
		}
	}

	if (pathid == CAM_XPT_PATH_ID)
		pathid = xptnextfreepathid();
	return (pathid);
}

static const char *
xpt_async_string(u_int32_t async_code)
{

	switch (async_code) {
	case AC_BUS_RESET: return ("AC_BUS_RESET");
	case AC_UNSOL_RESEL: return ("AC_UNSOL_RESEL");
	case AC_SCSI_AEN: return ("AC_SCSI_AEN");
	case AC_SENT_BDR: return ("AC_SENT_BDR");
	case AC_PATH_REGISTERED: return ("AC_PATH_REGISTERED");
	case AC_PATH_DEREGISTERED: return ("AC_PATH_DEREGISTERED");
	case AC_FOUND_DEVICE: return ("AC_FOUND_DEVICE");
	case AC_LOST_DEVICE: return ("AC_LOST_DEVICE");
	case AC_TRANSFER_NEG: return ("AC_TRANSFER_NEG");
	case AC_INQ_CHANGED: return ("AC_INQ_CHANGED");
	case AC_GETDEV_CHANGED: return ("AC_GETDEV_CHANGED");
	case AC_CONTRACT: return ("AC_CONTRACT");
	case AC_ADVINFO_CHANGED: return ("AC_ADVINFO_CHANGED");
	case AC_UNIT_ATTENTION: return ("AC_UNIT_ATTENTION");
	}
	return ("AC_UNKNOWN");
}

static int
xpt_async_size(u_int32_t async_code)
{

	switch (async_code) {
	case AC_BUS_RESET: return (0);
	case AC_UNSOL_RESEL: return (0);
	case AC_SCSI_AEN: return (0);
	case AC_SENT_BDR: return (0);
	case AC_PATH_REGISTERED: return (sizeof(struct ccb_pathinq));
	case AC_PATH_DEREGISTERED: return (0);
	case AC_FOUND_DEVICE: return (sizeof(struct ccb_getdev));
	case AC_LOST_DEVICE: return (0);
	case AC_TRANSFER_NEG: return (sizeof(struct ccb_trans_settings));
	case AC_INQ_CHANGED: return (0);
	case AC_GETDEV_CHANGED: return (0);
	case AC_CONTRACT: return (sizeof(struct ac_contract));
	case AC_ADVINFO_CHANGED: return (-1);
	case AC_UNIT_ATTENTION: return (sizeof(struct ccb_scsiio));
	}
	return (0);
}

static int
xpt_async_process_dev(struct cam_ed *device, void *arg)
{
	union ccb *ccb = arg;
	struct cam_path *path = ccb->ccb_h.path;
	void *async_arg = ccb->casync.async_arg_ptr;
	u_int32_t async_code = ccb->casync.async_code;
	int relock;

	if (path->device != device
	 && path->device->lun_id != CAM_LUN_WILDCARD
	 && device->lun_id != CAM_LUN_WILDCARD)
		return (1);

	/*
	 * The async callback could free the device.
	 * If it is a broadcast async, it doesn't hold
	 * device reference, so take our own reference.
	 */
	xpt_acquire_device(device);

	/*
	 * If async for specific device is to be delivered to
	 * the wildcard client, take the specific device lock.
	 * XXX: We may need a way for client to specify it.
	 */
	if ((device->lun_id == CAM_LUN_WILDCARD &&
	     path->device->lun_id != CAM_LUN_WILDCARD) ||
	    (device->target->target_id == CAM_TARGET_WILDCARD &&
	     path->target->target_id != CAM_TARGET_WILDCARD) ||
	    (device->target->bus->path_id == CAM_BUS_WILDCARD &&
	     path->target->bus->path_id != CAM_BUS_WILDCARD)) {
		mtx_unlock(&device->device_mtx);
		xpt_path_lock(path);
		relock = 1;
	} else
		relock = 0;

	(*(device->target->bus->xport->ops->async))(async_code,
	    device->target->bus, device->target, device, async_arg);
	xpt_async_bcast(&device->asyncs, async_code, path, async_arg);

	if (relock) {
		xpt_path_unlock(path);
		mtx_lock(&device->device_mtx);
	}
	xpt_release_device(device);
	return (1);
}

static int
xpt_async_process_tgt(struct cam_et *target, void *arg)
{
	union ccb *ccb = arg;
	struct cam_path *path = ccb->ccb_h.path;

	if (path->target != target
	 && path->target->target_id != CAM_TARGET_WILDCARD
	 && target->target_id != CAM_TARGET_WILDCARD)
		return (1);

	if (ccb->casync.async_code == AC_SENT_BDR) {
		/* Update our notion of when the last reset occurred */
		microtime(&target->last_reset);
	}

	return (xptdevicetraverse(target, NULL, xpt_async_process_dev, ccb));
}

static void
xpt_async_process(struct cam_periph *periph, union ccb *ccb)
{
	struct cam_eb *bus;
	struct cam_path *path;
	void *async_arg;
	u_int32_t async_code;

	path = ccb->ccb_h.path;
	async_code = ccb->casync.async_code;
	async_arg = ccb->casync.async_arg_ptr;
	CAM_DEBUG(path, CAM_DEBUG_TRACE | CAM_DEBUG_INFO,
	    ("xpt_async(%s)\n", xpt_async_string(async_code)));
	bus = path->bus;

	if (async_code == AC_BUS_RESET) {
		/* Update our notion of when the last reset occurred */
		microtime(&bus->last_reset);
	}

	xpttargettraverse(bus, NULL, xpt_async_process_tgt, ccb);

	/*
	 * If this wasn't a fully wildcarded async, tell all
	 * clients that want all async events.
	 */
	if (bus != xpt_periph->path->bus) {
		xpt_path_lock(xpt_periph->path);
		xpt_async_process_dev(xpt_periph->path->device, ccb);
		xpt_path_unlock(xpt_periph->path);
	}

	if (path->device != NULL && path->device->lun_id != CAM_LUN_WILDCARD)
		xpt_release_devq(path, 1, TRUE);
	else
		xpt_release_simq(path->bus->sim, TRUE);
	if (ccb->casync.async_arg_size > 0)
		free(async_arg, M_CAMXPT);
	xpt_free_path(path);
	xpt_free_ccb(ccb);
}

static void
xpt_async_bcast(struct async_list *async_head,
		u_int32_t async_code,
		struct cam_path *path, void *async_arg)
{
	struct async_node *cur_entry;
	struct mtx *mtx;

	cur_entry = SLIST_FIRST(async_head);
	while (cur_entry != NULL) {
		struct async_node *next_entry;
		/*
		 * Grab the next list entry before we call the current
		 * entry's callback.  This is because the callback function
		 * can delete its async callback entry.
		 */
		next_entry = SLIST_NEXT(cur_entry, links);
		if ((cur_entry->event_enable & async_code) != 0) {
			mtx = cur_entry->event_lock ?
			    path->device->sim->mtx : NULL;
			if (mtx)
				mtx_lock(mtx);
			cur_entry->callback(cur_entry->callback_arg,
					    async_code, path,
					    async_arg);
			if (mtx)
				mtx_unlock(mtx);
		}
		cur_entry = next_entry;
	}
}

void
xpt_async(u_int32_t async_code, struct cam_path *path, void *async_arg)
{
	union ccb *ccb;
	int size;

	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		xpt_print(path, "Can't allocate CCB to send %s\n",
		    xpt_async_string(async_code));
		return;
	}

	if (xpt_clone_path(&ccb->ccb_h.path, path) != CAM_REQ_CMP) {
		xpt_print(path, "Can't allocate path to send %s\n",
		    xpt_async_string(async_code));
		xpt_free_ccb(ccb);
		return;
	}
	ccb->ccb_h.path->periph = NULL;
	ccb->ccb_h.func_code = XPT_ASYNC;
	ccb->ccb_h.cbfcnp = xpt_async_process;
	ccb->ccb_h.flags |= CAM_UNLOCKED;
	ccb->casync.async_code = async_code;
	ccb->casync.async_arg_size = 0;
	size = xpt_async_size(async_code);
	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_async: func %#x %s aync_code %d %s\n",
		ccb->ccb_h.func_code,
		xpt_action_name(ccb->ccb_h.func_code),
		async_code,
		xpt_async_string(async_code)));
	if (size > 0 && async_arg != NULL) {
		ccb->casync.async_arg_ptr = malloc(size, M_CAMXPT, M_NOWAIT);
		if (ccb->casync.async_arg_ptr == NULL) {
			xpt_print(path, "Can't allocate argument to send %s\n",
			    xpt_async_string(async_code));
			xpt_free_path(ccb->ccb_h.path);
			xpt_free_ccb(ccb);
			return;
		}
		memcpy(ccb->casync.async_arg_ptr, async_arg, size);
		ccb->casync.async_arg_size = size;
	} else if (size < 0) {
		ccb->casync.async_arg_ptr = async_arg;
		ccb->casync.async_arg_size = size;
	}
	if (path->device != NULL && path->device->lun_id != CAM_LUN_WILDCARD)
		xpt_freeze_devq(path, 1);
	else
		xpt_freeze_simq(path->bus->sim, 1);
	xpt_done(ccb);
}

static void
xpt_dev_async_default(u_int32_t async_code, struct cam_eb *bus,
		      struct cam_et *target, struct cam_ed *device,
		      void *async_arg)
{

	/*
	 * We only need to handle events for real devices.
	 */
	if (target->target_id == CAM_TARGET_WILDCARD
	 || device->lun_id == CAM_LUN_WILDCARD)
		return;

	printf("%s called\n", __func__);
}

static uint32_t
xpt_freeze_devq_device(struct cam_ed *dev, u_int count)
{
	struct cam_devq	*devq;
	uint32_t freeze;

	devq = dev->sim->devq;
	mtx_assert(&devq->send_mtx, MA_OWNED);
	CAM_DEBUG_DEV(dev, CAM_DEBUG_TRACE,
	    ("xpt_freeze_devq_device(%d) %u->%u\n", count,
	    dev->ccbq.queue.qfrozen_cnt, dev->ccbq.queue.qfrozen_cnt + count));
	freeze = (dev->ccbq.queue.qfrozen_cnt += count);
	/* Remove frozen device from sendq. */
	if (device_is_queued(dev))
		camq_remove(&devq->send_queue, dev->devq_entry.index);
	return (freeze);
}

u_int32_t
xpt_freeze_devq(struct cam_path *path, u_int count)
{
	struct cam_ed	*dev = path->device;
	struct cam_devq	*devq;
	uint32_t	 freeze;

	devq = dev->sim->devq;
	mtx_lock(&devq->send_mtx);
	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_freeze_devq(%d)\n", count));
	freeze = xpt_freeze_devq_device(dev, count);
	mtx_unlock(&devq->send_mtx);
	return (freeze);
}

u_int32_t
xpt_freeze_simq(struct cam_sim *sim, u_int count)
{
	struct cam_devq	*devq;
	uint32_t	 freeze;

	devq = sim->devq;
	mtx_lock(&devq->send_mtx);
	freeze = (devq->send_queue.qfrozen_cnt += count);
	mtx_unlock(&devq->send_mtx);
	return (freeze);
}

static void
xpt_release_devq_timeout(void *arg)
{
	struct cam_ed *dev;
	struct cam_devq *devq;

	dev = (struct cam_ed *)arg;
	CAM_DEBUG_DEV(dev, CAM_DEBUG_TRACE, ("xpt_release_devq_timeout\n"));
	devq = dev->sim->devq;
	mtx_assert(&devq->send_mtx, MA_OWNED);
	if (xpt_release_devq_device(dev, /*count*/1, /*run_queue*/TRUE))
		xpt_run_devq(devq);
}

void
xpt_release_devq(struct cam_path *path, u_int count, int run_queue)
{
	struct cam_ed *dev;
	struct cam_devq *devq;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("xpt_release_devq(%d, %d)\n",
	    count, run_queue));
	dev = path->device;
	devq = dev->sim->devq;
	mtx_lock(&devq->send_mtx);
	if (xpt_release_devq_device(dev, count, run_queue))
		xpt_run_devq(dev->sim->devq);
	mtx_unlock(&devq->send_mtx);
}

static int
xpt_release_devq_device(struct cam_ed *dev, u_int count, int run_queue)
{

	mtx_assert(&dev->sim->devq->send_mtx, MA_OWNED);
	CAM_DEBUG_DEV(dev, CAM_DEBUG_TRACE,
	    ("xpt_release_devq_device(%d, %d) %u->%u\n", count, run_queue,
	    dev->ccbq.queue.qfrozen_cnt, dev->ccbq.queue.qfrozen_cnt - count));
	if (count > dev->ccbq.queue.qfrozen_cnt) {
#ifdef INVARIANTS
		printf("xpt_release_devq(): requested %u > present %u\n",
		    count, dev->ccbq.queue.qfrozen_cnt);
#endif
		count = dev->ccbq.queue.qfrozen_cnt;
	}
	dev->ccbq.queue.qfrozen_cnt -= count;
	if (dev->ccbq.queue.qfrozen_cnt == 0) {
		/*
		 * No longer need to wait for a successful
		 * command completion.
		 */
		dev->flags &= ~CAM_DEV_REL_ON_COMPLETE;
		/*
		 * Remove any timeouts that might be scheduled
		 * to release this queue.
		 */
		if ((dev->flags & CAM_DEV_REL_TIMEOUT_PENDING) != 0) {
			callout_stop(&dev->callout);
			dev->flags &= ~CAM_DEV_REL_TIMEOUT_PENDING;
		}
		/*
		 * Now that we are unfrozen schedule the
		 * device so any pending transactions are
		 * run.
		 */
		xpt_schedule_devq(dev->sim->devq, dev);
	} else
		run_queue = 0;
	return (run_queue);
}

void
xpt_release_simq(struct cam_sim *sim, int run_queue)
{
	struct cam_devq	*devq;

	devq = sim->devq;
	mtx_lock(&devq->send_mtx);
	if (devq->send_queue.qfrozen_cnt <= 0) {
#ifdef INVARIANTS
		printf("xpt_release_simq: requested 1 > present %u\n",
		    devq->send_queue.qfrozen_cnt);
#endif
	} else
		devq->send_queue.qfrozen_cnt--;
	if (devq->send_queue.qfrozen_cnt == 0) {
		/*
		 * If there is a timeout scheduled to release this
		 * sim queue, remove it.  The queue frozen count is
		 * already at 0.
		 */
		if ((sim->flags & CAM_SIM_REL_TIMEOUT_PENDING) != 0){
			callout_stop(&sim->callout);
			sim->flags &= ~CAM_SIM_REL_TIMEOUT_PENDING;
		}
		if (run_queue) {
			/*
			 * Now that we are unfrozen run the send queue.
			 */
			xpt_run_devq(sim->devq);
		}
	}
	mtx_unlock(&devq->send_mtx);
}

/*
 * XXX Appears to be unused.
 */
static void
xpt_release_simq_timeout(void *arg)
{
	struct cam_sim *sim;

	sim = (struct cam_sim *)arg;
	xpt_release_simq(sim, /* run_queue */ TRUE);
}

void
xpt_done(union ccb *done_ccb)
{
	struct cam_doneq *queue;
	int	run, hash;

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (done_ccb->ccb_h.func_code == XPT_SCSI_IO &&
	    done_ccb->csio.bio != NULL)
		biotrack(done_ccb->csio.bio, __func__);
#endif

	CAM_DEBUG(done_ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_done: func= %#x %s status %#x\n",
		done_ccb->ccb_h.func_code,
		xpt_action_name(done_ccb->ccb_h.func_code),
		done_ccb->ccb_h.status));
	if ((done_ccb->ccb_h.func_code & XPT_FC_QUEUED) == 0)
		return;

	/* Store the time the ccb was in the sim */
	done_ccb->ccb_h.qos.periph_data = cam_iosched_delta_t(done_ccb->ccb_h.qos.periph_data);
	hash = (done_ccb->ccb_h.path_id + done_ccb->ccb_h.target_id +
	    done_ccb->ccb_h.target_lun) % cam_num_doneqs;
	queue = &cam_doneqs[hash];
	mtx_lock(&queue->cam_doneq_mtx);
	run = (queue->cam_doneq_sleep && STAILQ_EMPTY(&queue->cam_doneq));
	STAILQ_INSERT_TAIL(&queue->cam_doneq, &done_ccb->ccb_h, sim_links.stqe);
	done_ccb->ccb_h.pinfo.index = CAM_DONEQ_INDEX;
	mtx_unlock(&queue->cam_doneq_mtx);
	if (run)
		wakeup(&queue->cam_doneq);
}

void
xpt_done_direct(union ccb *done_ccb)
{

	CAM_DEBUG(done_ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_done_direct: status %#x\n", done_ccb->ccb_h.status));
	if ((done_ccb->ccb_h.func_code & XPT_FC_QUEUED) == 0)
		return;

	/* Store the time the ccb was in the sim */
	done_ccb->ccb_h.qos.periph_data = cam_iosched_delta_t(done_ccb->ccb_h.qos.periph_data);
	xpt_done_process(&done_ccb->ccb_h);
}

union ccb *
xpt_alloc_ccb()
{
	union ccb *new_ccb;

	new_ccb = malloc(sizeof(*new_ccb), M_CAMCCB, M_ZERO|M_WAITOK);
	return (new_ccb);
}

union ccb *
xpt_alloc_ccb_nowait()
{
	union ccb *new_ccb;

	new_ccb = malloc(sizeof(*new_ccb), M_CAMCCB, M_ZERO|M_NOWAIT);
	return (new_ccb);
}

void
xpt_free_ccb(union ccb *free_ccb)
{
	free(free_ccb, M_CAMCCB);
}



/* Private XPT functions */

/*
 * Get a CAM control block for the caller. Charge the structure to the device
 * referenced by the path.  If we don't have sufficient resources to allocate
 * more ccbs, we return NULL.
 */
static union ccb *
xpt_get_ccb_nowait(struct cam_periph *periph)
{
	union ccb *new_ccb;

	new_ccb = malloc(sizeof(*new_ccb), M_CAMCCB, M_ZERO|M_NOWAIT);
	if (new_ccb == NULL)
		return (NULL);
	periph->periph_allocated++;
	cam_ccbq_take_opening(&periph->path->device->ccbq);
	return (new_ccb);
}

static union ccb *
xpt_get_ccb(struct cam_periph *periph)
{
	union ccb *new_ccb;

	cam_periph_unlock(periph);
	new_ccb = malloc(sizeof(*new_ccb), M_CAMCCB, M_ZERO|M_WAITOK);
	cam_periph_lock(periph);
	periph->periph_allocated++;
	cam_ccbq_take_opening(&periph->path->device->ccbq);
	return (new_ccb);
}

union ccb *
cam_periph_getccb(struct cam_periph *periph, u_int32_t priority)
{
	struct ccb_hdr *ccb_h;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("cam_periph_getccb\n"));
	cam_periph_assert(periph, MA_OWNED);
	while ((ccb_h = SLIST_FIRST(&periph->ccb_list)) == NULL ||
	    ccb_h->pinfo.priority != priority) {
		if (priority < periph->immediate_priority) {
			periph->immediate_priority = priority;
			xpt_run_allocq(periph, 0);
		} else
			cam_periph_sleep(periph, &periph->ccb_list, PRIBIO,
			    "cgticb", 0);
	}
	SLIST_REMOVE_HEAD(&periph->ccb_list, periph_links.sle);
	return ((union ccb *)ccb_h);
}

static void
xpt_acquire_bus(struct cam_eb *bus)
{

	xpt_lock_buses();
	bus->refcount++;
	xpt_unlock_buses();
}

static void
xpt_release_bus(struct cam_eb *bus)
{

	xpt_lock_buses();
	KASSERT(bus->refcount >= 1, ("bus->refcount >= 1"));
	if (--bus->refcount > 0) {
		xpt_unlock_buses();
		return;
	}
	TAILQ_REMOVE(&xsoftc.xpt_busses, bus, links);
	xsoftc.bus_generation++;
	xpt_unlock_buses();
	KASSERT(TAILQ_EMPTY(&bus->et_entries),
	    ("destroying bus, but target list is not empty"));
	cam_sim_release(bus->sim);
	mtx_destroy(&bus->eb_mtx);
	free(bus, M_CAMXPT);
}

static struct cam_et *
xpt_alloc_target(struct cam_eb *bus, target_id_t target_id)
{
	struct cam_et *cur_target, *target;

	mtx_assert(&xsoftc.xpt_topo_lock, MA_OWNED);
	mtx_assert(&bus->eb_mtx, MA_OWNED);
	target = (struct cam_et *)malloc(sizeof(*target), M_CAMXPT,
					 M_NOWAIT|M_ZERO);
	if (target == NULL)
		return (NULL);

	TAILQ_INIT(&target->ed_entries);
	target->bus = bus;
	target->target_id = target_id;
	target->refcount = 1;
	target->generation = 0;
	target->luns = NULL;
	mtx_init(&target->luns_mtx, "CAM LUNs lock", NULL, MTX_DEF);
	timevalclear(&target->last_reset);
	/*
	 * Hold a reference to our parent bus so it
	 * will not go away before we do.
	 */
	bus->refcount++;

	/* Insertion sort into our bus's target list */
	cur_target = TAILQ_FIRST(&bus->et_entries);
	while (cur_target != NULL && cur_target->target_id < target_id)
		cur_target = TAILQ_NEXT(cur_target, links);
	if (cur_target != NULL) {
		TAILQ_INSERT_BEFORE(cur_target, target, links);
	} else {
		TAILQ_INSERT_TAIL(&bus->et_entries, target, links);
	}
	bus->generation++;
	return (target);
}

static void
xpt_acquire_target(struct cam_et *target)
{
	struct cam_eb *bus = target->bus;

	mtx_lock(&bus->eb_mtx);
	target->refcount++;
	mtx_unlock(&bus->eb_mtx);
}

static void
xpt_release_target(struct cam_et *target)
{
	struct cam_eb *bus = target->bus;

	mtx_lock(&bus->eb_mtx);
	if (--target->refcount > 0) {
		mtx_unlock(&bus->eb_mtx);
		return;
	}
	TAILQ_REMOVE(&bus->et_entries, target, links);
	bus->generation++;
	mtx_unlock(&bus->eb_mtx);
	KASSERT(TAILQ_EMPTY(&target->ed_entries),
	    ("destroying target, but device list is not empty"));
	xpt_release_bus(bus);
	mtx_destroy(&target->luns_mtx);
	if (target->luns)
		free(target->luns, M_CAMXPT);
	free(target, M_CAMXPT);
}

static struct cam_ed *
xpt_alloc_device_default(struct cam_eb *bus, struct cam_et *target,
			 lun_id_t lun_id)
{
	struct cam_ed *device;

	device = xpt_alloc_device(bus, target, lun_id);
	if (device == NULL)
		return (NULL);

	device->mintags = 1;
	device->maxtags = 1;
	return (device);
}

static void
xpt_destroy_device(void *context, int pending)
{
	struct cam_ed	*device = context;

	mtx_lock(&device->device_mtx);
	mtx_destroy(&device->device_mtx);
	free(device, M_CAMDEV);
}

struct cam_ed *
xpt_alloc_device(struct cam_eb *bus, struct cam_et *target, lun_id_t lun_id)
{
	struct cam_ed	*cur_device, *device;
	struct cam_devq	*devq;
	cam_status status;

	mtx_assert(&bus->eb_mtx, MA_OWNED);
	/* Make space for us in the device queue on our bus */
	devq = bus->sim->devq;
	mtx_lock(&devq->send_mtx);
	status = cam_devq_resize(devq, devq->send_queue.array_size + 1);
	mtx_unlock(&devq->send_mtx);
	if (status != CAM_REQ_CMP)
		return (NULL);

	device = (struct cam_ed *)malloc(sizeof(*device),
					 M_CAMDEV, M_NOWAIT|M_ZERO);
	if (device == NULL)
		return (NULL);

	cam_init_pinfo(&device->devq_entry);
	device->target = target;
	device->lun_id = lun_id;
	device->sim = bus->sim;
	if (cam_ccbq_init(&device->ccbq,
			  bus->sim->max_dev_openings) != 0) {
		free(device, M_CAMDEV);
		return (NULL);
	}
	SLIST_INIT(&device->asyncs);
	SLIST_INIT(&device->periphs);
	device->generation = 0;
	device->flags = CAM_DEV_UNCONFIGURED;
	device->tag_delay_count = 0;
	device->tag_saved_openings = 0;
	device->refcount = 1;
	mtx_init(&device->device_mtx, "CAM device lock", NULL, MTX_DEF);
	callout_init_mtx(&device->callout, &devq->send_mtx, 0);
	TASK_INIT(&device->device_destroy_task, 0, xpt_destroy_device, device);
	/*
	 * Hold a reference to our parent bus so it
	 * will not go away before we do.
	 */
	target->refcount++;

	cur_device = TAILQ_FIRST(&target->ed_entries);
	while (cur_device != NULL && cur_device->lun_id < lun_id)
		cur_device = TAILQ_NEXT(cur_device, links);
	if (cur_device != NULL)
		TAILQ_INSERT_BEFORE(cur_device, device, links);
	else
		TAILQ_INSERT_TAIL(&target->ed_entries, device, links);
	target->generation++;
	return (device);
}

void
xpt_acquire_device(struct cam_ed *device)
{
	struct cam_eb *bus = device->target->bus;

	mtx_lock(&bus->eb_mtx);
	device->refcount++;
	mtx_unlock(&bus->eb_mtx);
}

void
xpt_release_device(struct cam_ed *device)
{
	struct cam_eb *bus = device->target->bus;
	struct cam_devq *devq;

	mtx_lock(&bus->eb_mtx);
	if (--device->refcount > 0) {
		mtx_unlock(&bus->eb_mtx);
		return;
	}

	TAILQ_REMOVE(&device->target->ed_entries, device,links);
	device->target->generation++;
	mtx_unlock(&bus->eb_mtx);

	/* Release our slot in the devq */
	devq = bus->sim->devq;
	mtx_lock(&devq->send_mtx);
	cam_devq_resize(devq, devq->send_queue.array_size - 1);
	mtx_unlock(&devq->send_mtx);

	KASSERT(SLIST_EMPTY(&device->periphs),
	    ("destroying device, but periphs list is not empty"));
	KASSERT(device->devq_entry.index == CAM_UNQUEUED_INDEX,
	    ("destroying device while still queued for ccbs"));

	if ((device->flags & CAM_DEV_REL_TIMEOUT_PENDING) != 0)
		callout_stop(&device->callout);

	xpt_release_target(device->target);

	cam_ccbq_fini(&device->ccbq);
	/*
	 * Free allocated memory.  free(9) does nothing if the
	 * supplied pointer is NULL, so it is safe to call without
	 * checking.
	 */
	free(device->supported_vpds, M_CAMXPT);
	free(device->device_id, M_CAMXPT);
	free(device->ext_inq, M_CAMXPT);
	free(device->physpath, M_CAMXPT);
	free(device->rcap_buf, M_CAMXPT);
	free(device->serial_num, M_CAMXPT);
	free(device->nvme_data, M_CAMXPT);
	free(device->nvme_cdata, M_CAMXPT);
	taskqueue_enqueue(xsoftc.xpt_taskq, &device->device_destroy_task);
}

u_int32_t
xpt_dev_ccbq_resize(struct cam_path *path, int newopenings)
{
	int	result;
	struct	cam_ed *dev;

	dev = path->device;
	mtx_lock(&dev->sim->devq->send_mtx);
	result = cam_ccbq_resize(&dev->ccbq, newopenings);
	mtx_unlock(&dev->sim->devq->send_mtx);
	if ((dev->flags & CAM_DEV_TAG_AFTER_COUNT) != 0
	 || (dev->inq_flags & SID_CmdQue) != 0)
		dev->tag_saved_openings = newopenings;
	return (result);
}

static struct cam_eb *
xpt_find_bus(path_id_t path_id)
{
	struct cam_eb *bus;

	xpt_lock_buses();
	for (bus = TAILQ_FIRST(&xsoftc.xpt_busses);
	     bus != NULL;
	     bus = TAILQ_NEXT(bus, links)) {
		if (bus->path_id == path_id) {
			bus->refcount++;
			break;
		}
	}
	xpt_unlock_buses();
	return (bus);
}

static struct cam_et *
xpt_find_target(struct cam_eb *bus, target_id_t	target_id)
{
	struct cam_et *target;

	mtx_assert(&bus->eb_mtx, MA_OWNED);
	for (target = TAILQ_FIRST(&bus->et_entries);
	     target != NULL;
	     target = TAILQ_NEXT(target, links)) {
		if (target->target_id == target_id) {
			target->refcount++;
			break;
		}
	}
	return (target);
}

static struct cam_ed *
xpt_find_device(struct cam_et *target, lun_id_t lun_id)
{
	struct cam_ed *device;

	mtx_assert(&target->bus->eb_mtx, MA_OWNED);
	for (device = TAILQ_FIRST(&target->ed_entries);
	     device != NULL;
	     device = TAILQ_NEXT(device, links)) {
		if (device->lun_id == lun_id) {
			device->refcount++;
			break;
		}
	}
	return (device);
}

void
xpt_start_tags(struct cam_path *path)
{
	struct ccb_relsim crs;
	struct cam_ed *device;
	struct cam_sim *sim;
	int    newopenings;

	device = path->device;
	sim = path->bus->sim;
	device->flags &= ~CAM_DEV_TAG_AFTER_COUNT;
	xpt_freeze_devq(path, /*count*/1);
	device->inq_flags |= SID_CmdQue;
	if (device->tag_saved_openings != 0)
		newopenings = device->tag_saved_openings;
	else
		newopenings = min(device->maxtags,
				  sim->max_tagged_dev_openings);
	xpt_dev_ccbq_resize(path, newopenings);
	xpt_async(AC_GETDEV_CHANGED, path, NULL);
	xpt_setup_ccb(&crs.ccb_h, path, CAM_PRIORITY_NORMAL);
	crs.ccb_h.func_code = XPT_REL_SIMQ;
	crs.release_flags = RELSIM_RELEASE_AFTER_QEMPTY;
	crs.openings
	    = crs.release_timeout
	    = crs.qfrozen_cnt
	    = 0;
	xpt_action((union ccb *)&crs);
}

void
xpt_stop_tags(struct cam_path *path)
{
	struct ccb_relsim crs;
	struct cam_ed *device;
	struct cam_sim *sim;

	device = path->device;
	sim = path->bus->sim;
	device->flags &= ~CAM_DEV_TAG_AFTER_COUNT;
	device->tag_delay_count = 0;
	xpt_freeze_devq(path, /*count*/1);
	device->inq_flags &= ~SID_CmdQue;
	xpt_dev_ccbq_resize(path, sim->max_dev_openings);
	xpt_async(AC_GETDEV_CHANGED, path, NULL);
	xpt_setup_ccb(&crs.ccb_h, path, CAM_PRIORITY_NORMAL);
	crs.ccb_h.func_code = XPT_REL_SIMQ;
	crs.release_flags = RELSIM_RELEASE_AFTER_QEMPTY;
	crs.openings
	    = crs.release_timeout
	    = crs.qfrozen_cnt
	    = 0;
	xpt_action((union ccb *)&crs);
}

static void
xpt_boot_delay(void *arg)
{

	xpt_release_boot();
}

static void
xpt_config(void *arg)
{
	/*
	 * Now that interrupts are enabled, go find our devices
	 */
	if (taskqueue_start_threads(&xsoftc.xpt_taskq, 1, PRIBIO, "CAM taskq"))
		printf("xpt_config: failed to create taskqueue thread.\n");

	/* Setup debugging path */
	if (cam_dflags != CAM_DEBUG_NONE) {
		if (xpt_create_path(&cam_dpath, NULL,
				    CAM_DEBUG_BUS, CAM_DEBUG_TARGET,
				    CAM_DEBUG_LUN) != CAM_REQ_CMP) {
			printf("xpt_config: xpt_create_path() failed for debug"
			       " target %d:%d:%d, debugging disabled\n",
			       CAM_DEBUG_BUS, CAM_DEBUG_TARGET, CAM_DEBUG_LUN);
			cam_dflags = CAM_DEBUG_NONE;
		}
	} else
		cam_dpath = NULL;

	periphdriver_init(1);
	xpt_hold_boot();
	callout_init(&xsoftc.boot_callout, 1);
	callout_reset_sbt(&xsoftc.boot_callout, SBT_1MS * xsoftc.boot_delay, 0,
	    xpt_boot_delay, NULL, 0);
	/* Fire up rescan thread. */
	if (kproc_kthread_add(xpt_scanner_thread, NULL, &cam_proc, NULL, 0, 0,
	    "cam", "scanner")) {
		printf("xpt_config: failed to create rescan thread.\n");
	}
}

void
xpt_hold_boot(void)
{
	xpt_lock_buses();
	xsoftc.buses_to_config++;
	xpt_unlock_buses();
}

void
xpt_release_boot(void)
{
	xpt_lock_buses();
	xsoftc.buses_to_config--;
	if (xsoftc.buses_to_config == 0 && xsoftc.buses_config_done == 0) {
		struct	xpt_task *task;

		xsoftc.buses_config_done = 1;
		xpt_unlock_buses();
		/* Call manually because we don't have any buses */
		task = malloc(sizeof(struct xpt_task), M_CAMXPT, M_NOWAIT);
		if (task != NULL) {
			TASK_INIT(&task->task, 0, xpt_finishconfig_task, task);
			taskqueue_enqueue(taskqueue_thread, &task->task);
		}
	} else
		xpt_unlock_buses();
}

/*
 * If the given device only has one peripheral attached to it, and if that
 * peripheral is the passthrough driver, announce it.  This insures that the
 * user sees some sort of announcement for every peripheral in their system.
 */
static int
xptpassannouncefunc(struct cam_ed *device, void *arg)
{
	struct cam_periph *periph;
	int i;

	for (periph = SLIST_FIRST(&device->periphs), i = 0; periph != NULL;
	     periph = SLIST_NEXT(periph, periph_links), i++);

	periph = SLIST_FIRST(&device->periphs);
	if ((i == 1)
	 && (strncmp(periph->periph_name, "pass", 4) == 0))
		xpt_announce_periph(periph, NULL);

	return(1);
}

static void
xpt_finishconfig_task(void *context, int pending)
{

	periphdriver_init(2);
	/*
	 * Check for devices with no "standard" peripheral driver
	 * attached.  For any devices like that, announce the
	 * passthrough driver so the user will see something.
	 */
	if (!bootverbose)
		xpt_for_all_devices(xptpassannouncefunc, NULL);

	/* Release our hook so that the boot can continue. */
	config_intrhook_disestablish(&xsoftc.xpt_config_hook);

	free(context, M_CAMXPT);
}

cam_status
xpt_register_async(int event, ac_callback_t *cbfunc, void *cbarg,
		   struct cam_path *path)
{
	struct ccb_setasync csa;
	cam_status status;
	int xptpath = 0;

	if (path == NULL) {
		status = xpt_create_path(&path, /*periph*/NULL, CAM_XPT_PATH_ID,
					 CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);
		if (status != CAM_REQ_CMP)
			return (status);
		xpt_path_lock(path);
		xptpath = 1;
	}

	xpt_setup_ccb(&csa.ccb_h, path, CAM_PRIORITY_NORMAL);
	csa.ccb_h.func_code = XPT_SASYNC_CB;
	csa.event_enable = event;
	csa.callback = cbfunc;
	csa.callback_arg = cbarg;
	xpt_action((union ccb *)&csa);
	status = csa.ccb_h.status;

	CAM_DEBUG(csa.ccb_h.path, CAM_DEBUG_TRACE,
	    ("xpt_register_async: func %p\n", cbfunc));

	if (xptpath) {
		xpt_path_unlock(path);
		xpt_free_path(path);
	}

	if ((status == CAM_REQ_CMP) &&
	    (csa.event_enable & AC_FOUND_DEVICE)) {
		/*
		 * Get this peripheral up to date with all
		 * the currently existing devices.
		 */
		xpt_for_all_devices(xptsetasyncfunc, &csa);
	}
	if ((status == CAM_REQ_CMP) &&
	    (csa.event_enable & AC_PATH_REGISTERED)) {
		/*
		 * Get this peripheral up to date with all
		 * the currently existing buses.
		 */
		xpt_for_all_busses(xptsetasyncbusfunc, &csa);
	}

	return (status);
}

static void
xptaction(struct cam_sim *sim, union ccb *work_ccb)
{
	CAM_DEBUG(work_ccb->ccb_h.path, CAM_DEBUG_TRACE, ("xptaction\n"));

	switch (work_ccb->ccb_h.func_code) {
	/* Common cases first */
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq *cpi;

		cpi = &work_ccb->cpi;
		cpi->version_num = 1; /* XXX??? */
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_misc = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = 0;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "", HBA_IDLEN);
		strlcpy(cpi->dev_name, sim->sim_name, DEV_IDLEN);
		cpi->unit_number = sim->unit_number;
		cpi->bus_id = sim->bus_id;
		cpi->base_transfer_speed = 0;
		cpi->protocol = PROTO_UNSPECIFIED;
		cpi->protocol_version = PROTO_VERSION_UNSPECIFIED;
		cpi->transport = XPORT_UNSPECIFIED;
		cpi->transport_version = XPORT_VERSION_UNSPECIFIED;
		cpi->ccb_h.status = CAM_REQ_CMP;
		xpt_done(work_ccb);
		break;
	}
	default:
		work_ccb->ccb_h.status = CAM_REQ_INVALID;
		xpt_done(work_ccb);
		break;
	}
}

/*
 * The xpt as a "controller" has no interrupt sources, so polling
 * is a no-op.
 */
static void
xptpoll(struct cam_sim *sim)
{
}

void
xpt_lock_buses(void)
{
	mtx_lock(&xsoftc.xpt_topo_lock);
}

void
xpt_unlock_buses(void)
{
	mtx_unlock(&xsoftc.xpt_topo_lock);
}

struct mtx *
xpt_path_mtx(struct cam_path *path)
{

	return (&path->device->device_mtx);
}

static void
xpt_done_process(struct ccb_hdr *ccb_h)
{
	struct cam_sim *sim = NULL;
	struct cam_devq *devq = NULL;
	struct mtx *mtx = NULL;

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	struct ccb_scsiio *csio;

	if (ccb_h->func_code == XPT_SCSI_IO) {
		csio = &((union ccb *)ccb_h)->csio;
		if (csio->bio != NULL)
			biotrack(csio->bio, __func__);
	}
#endif

	if (ccb_h->flags & CAM_HIGH_POWER) {
		struct highpowerlist	*hphead;
		struct cam_ed		*device;

		mtx_lock(&xsoftc.xpt_highpower_lock);
		hphead = &xsoftc.highpowerq;

		device = STAILQ_FIRST(hphead);

		/*
		 * Increment the count since this command is done.
		 */
		xsoftc.num_highpower++;

		/*
		 * Any high powered commands queued up?
		 */
		if (device != NULL) {

			STAILQ_REMOVE_HEAD(hphead, highpowerq_entry);
			mtx_unlock(&xsoftc.xpt_highpower_lock);

			mtx_lock(&device->sim->devq->send_mtx);
			xpt_release_devq_device(device,
					 /*count*/1, /*runqueue*/TRUE);
			mtx_unlock(&device->sim->devq->send_mtx);
		} else
			mtx_unlock(&xsoftc.xpt_highpower_lock);
	}

	/*
	 * Insulate against a race where the periph is destroyed but CCBs are
	 * still not all processed. This shouldn't happen, but allows us better
	 * bug diagnostic when it does.
	 */
	if (ccb_h->path->bus)
		sim = ccb_h->path->bus->sim;

	if (ccb_h->status & CAM_RELEASE_SIMQ) {
		KASSERT(sim, ("sim missing for CAM_RELEASE_SIMQ request"));
		xpt_release_simq(sim, /*run_queue*/FALSE);
		ccb_h->status &= ~CAM_RELEASE_SIMQ;
	}

	if ((ccb_h->flags & CAM_DEV_QFRZDIS)
	 && (ccb_h->status & CAM_DEV_QFRZN)) {
		xpt_release_devq(ccb_h->path, /*count*/1, /*run_queue*/TRUE);
		ccb_h->status &= ~CAM_DEV_QFRZN;
	}

	if ((ccb_h->func_code & XPT_FC_USER_CCB) == 0) {
		struct cam_ed *dev = ccb_h->path->device;

		if (sim)
			devq = sim->devq;
		KASSERT(devq, ("Periph disappeared with request pending."));

		mtx_lock(&devq->send_mtx);
		devq->send_active--;
		devq->send_openings++;
		cam_ccbq_ccb_done(&dev->ccbq, (union ccb *)ccb_h);

		if (((dev->flags & CAM_DEV_REL_ON_QUEUE_EMPTY) != 0
		  && (dev->ccbq.dev_active == 0))) {
			dev->flags &= ~CAM_DEV_REL_ON_QUEUE_EMPTY;
			xpt_release_devq_device(dev, /*count*/1,
					 /*run_queue*/FALSE);
		}

		if (((dev->flags & CAM_DEV_REL_ON_COMPLETE) != 0
		  && (ccb_h->status&CAM_STATUS_MASK) != CAM_REQUEUE_REQ)) {
			dev->flags &= ~CAM_DEV_REL_ON_COMPLETE;
			xpt_release_devq_device(dev, /*count*/1,
					 /*run_queue*/FALSE);
		}

		if (!device_is_queued(dev))
			(void)xpt_schedule_devq(devq, dev);
		xpt_run_devq(devq);
		mtx_unlock(&devq->send_mtx);

		if ((dev->flags & CAM_DEV_TAG_AFTER_COUNT) != 0) {
			mtx = xpt_path_mtx(ccb_h->path);
			mtx_lock(mtx);

			if ((dev->flags & CAM_DEV_TAG_AFTER_COUNT) != 0
			 && (--dev->tag_delay_count == 0))
				xpt_start_tags(ccb_h->path);
		}
	}

	if ((ccb_h->flags & CAM_UNLOCKED) == 0) {
		if (mtx == NULL) {
			mtx = xpt_path_mtx(ccb_h->path);
			mtx_lock(mtx);
		}
	} else {
		if (mtx != NULL) {
			mtx_unlock(mtx);
			mtx = NULL;
		}
	}

	/* Call the peripheral driver's callback */
	ccb_h->pinfo.index = CAM_UNQUEUED_INDEX;
	(*ccb_h->cbfcnp)(ccb_h->path->periph, (union ccb *)ccb_h);
	if (mtx != NULL)
		mtx_unlock(mtx);
}

void
xpt_done_td(void *arg)
{
	struct cam_doneq *queue = arg;
	struct ccb_hdr *ccb_h;
	STAILQ_HEAD(, ccb_hdr)	doneq;

	STAILQ_INIT(&doneq);
	mtx_lock(&queue->cam_doneq_mtx);
	while (1) {
		while (STAILQ_EMPTY(&queue->cam_doneq)) {
			queue->cam_doneq_sleep = 1;
			msleep(&queue->cam_doneq, &queue->cam_doneq_mtx,
			    PRIBIO, "-", 0);
			queue->cam_doneq_sleep = 0;
		}
		STAILQ_CONCAT(&doneq, &queue->cam_doneq);
		mtx_unlock(&queue->cam_doneq_mtx);

		THREAD_NO_SLEEPING();
		while ((ccb_h = STAILQ_FIRST(&doneq)) != NULL) {
			STAILQ_REMOVE_HEAD(&doneq, sim_links.stqe);
			xpt_done_process(ccb_h);
		}
		THREAD_SLEEPING_OK();

		mtx_lock(&queue->cam_doneq_mtx);
	}
}

static void
camisr_runqueue(void)
{
	struct	ccb_hdr *ccb_h;
	struct cam_doneq *queue;
	int i;

	/* Process global queues. */
	for (i = 0; i < cam_num_doneqs; i++) {
		queue = &cam_doneqs[i];
		mtx_lock(&queue->cam_doneq_mtx);
		while ((ccb_h = STAILQ_FIRST(&queue->cam_doneq)) != NULL) {
			STAILQ_REMOVE_HEAD(&queue->cam_doneq, sim_links.stqe);
			mtx_unlock(&queue->cam_doneq_mtx);
			xpt_done_process(ccb_h);
			mtx_lock(&queue->cam_doneq_mtx);
		}
		mtx_unlock(&queue->cam_doneq_mtx);
	}
}

struct kv 
{
	uint32_t v;
	const char *name;
};

static struct kv map[] = {
	{ XPT_NOOP, "XPT_NOOP" },
	{ XPT_SCSI_IO, "XPT_SCSI_IO" },
	{ XPT_GDEV_TYPE, "XPT_GDEV_TYPE" },
	{ XPT_GDEVLIST, "XPT_GDEVLIST" },
	{ XPT_PATH_INQ, "XPT_PATH_INQ" },
	{ XPT_REL_SIMQ, "XPT_REL_SIMQ" },
	{ XPT_SASYNC_CB, "XPT_SASYNC_CB" },
	{ XPT_SDEV_TYPE, "XPT_SDEV_TYPE" },
	{ XPT_SCAN_BUS, "XPT_SCAN_BUS" },
	{ XPT_DEV_MATCH, "XPT_DEV_MATCH" },
	{ XPT_DEBUG, "XPT_DEBUG" },
	{ XPT_PATH_STATS, "XPT_PATH_STATS" },
	{ XPT_GDEV_STATS, "XPT_GDEV_STATS" },
	{ XPT_DEV_ADVINFO, "XPT_DEV_ADVINFO" },
	{ XPT_ASYNC, "XPT_ASYNC" },
	{ XPT_ABORT, "XPT_ABORT" },
	{ XPT_RESET_BUS, "XPT_RESET_BUS" },
	{ XPT_RESET_DEV, "XPT_RESET_DEV" },
	{ XPT_TERM_IO, "XPT_TERM_IO" },
	{ XPT_SCAN_LUN, "XPT_SCAN_LUN" },
	{ XPT_GET_TRAN_SETTINGS, "XPT_GET_TRAN_SETTINGS" },
	{ XPT_SET_TRAN_SETTINGS, "XPT_SET_TRAN_SETTINGS" },
	{ XPT_CALC_GEOMETRY, "XPT_CALC_GEOMETRY" },
	{ XPT_ATA_IO, "XPT_ATA_IO" },
	{ XPT_GET_SIM_KNOB, "XPT_GET_SIM_KNOB" },
	{ XPT_SET_SIM_KNOB, "XPT_SET_SIM_KNOB" },
	{ XPT_NVME_IO, "XPT_NVME_IO" },
	{ XPT_MMC_IO, "XPT_MMC_IO" },
	{ XPT_SMP_IO, "XPT_SMP_IO" },
	{ XPT_SCAN_TGT, "XPT_SCAN_TGT" },
	{ XPT_NVME_ADMIN, "XPT_NVME_ADMIN" },
	{ XPT_ENG_INQ, "XPT_ENG_INQ" },
	{ XPT_ENG_EXEC, "XPT_ENG_EXEC" },
	{ XPT_EN_LUN, "XPT_EN_LUN" },
	{ XPT_TARGET_IO, "XPT_TARGET_IO" },
	{ XPT_ACCEPT_TARGET_IO, "XPT_ACCEPT_TARGET_IO" },
	{ XPT_CONT_TARGET_IO, "XPT_CONT_TARGET_IO" },
	{ XPT_IMMED_NOTIFY, "XPT_IMMED_NOTIFY" },
	{ XPT_NOTIFY_ACK, "XPT_NOTIFY_ACK" },
	{ XPT_IMMEDIATE_NOTIFY, "XPT_IMMEDIATE_NOTIFY" },
	{ XPT_NOTIFY_ACKNOWLEDGE, "XPT_NOTIFY_ACKNOWLEDGE" },
	{ 0, 0 }
};

const char *
xpt_action_name(uint32_t action) 
{
	static char buffer[32];	/* Only for unknown messages -- racy */
	struct kv *walker = map;

	while (walker->name != NULL) {
		if (walker->v == action)
			return (walker->name);
		walker++;
	}

	snprintf(buffer, sizeof(buffer), "%#x", action);
	return (buffer);
}
