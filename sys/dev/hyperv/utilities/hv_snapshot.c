/*-
 * Copyright (c) 2016 Microsoft Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/taskqueue.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/un.h>
#include <sys/endian.h>
#include <sys/sema.h>
#include <sys/signal.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <sys/callout.h>

#include <dev/hyperv/include/hyperv.h>
#include <dev/hyperv/utilities/hv_utilreg.h>
#include <dev/hyperv/utilities/vmbus_icreg.h>
#include <dev/hyperv/utilities/vmbus_icvar.h>

#include "hv_snapshot.h"
#include "vmbus_if.h"

#define VSS_MAJOR		5
#define VSS_MINOR		0
#define VSS_MSGVER		VMBUS_IC_VERSION(VSS_MAJOR, VSS_MINOR)

#define VSS_FWVER_MAJOR		3
#define VSS_FWVER		VMBUS_IC_VERSION(VSS_FWVER_MAJOR, 0)

#define TIMEOUT_LIMIT		(15)	// seconds
enum hv_vss_op {
	VSS_OP_CREATE = 0,
	VSS_OP_DELETE,
	VSS_OP_HOT_BACKUP,
	VSS_OP_GET_DM_INFO,
	VSS_OP_BU_COMPLETE,
	/*
	 * Following operations are only supported with IC version >= 5.0
	 */
	VSS_OP_FREEZE, /* Freeze the file systems in the VM */
	VSS_OP_THAW, /* Unfreeze the file systems */
	VSS_OP_AUTO_RECOVER,
	VSS_OP_COUNT /* Number of operations, must be last */
};

/*
 * Header for all VSS messages.
 */
struct hv_vss_hdr {
	struct vmbus_icmsg_hdr	ic_hdr;
	uint8_t			operation;
	uint8_t			reserved[7];
} __packed;


/*
 * Flag values for the hv_vss_check_feature. Here supports only
 * one value.
 */
#define VSS_HBU_NO_AUTO_RECOVERY		0x00000005

struct hv_vss_check_feature {
	uint32_t flags;
} __packed;

struct hv_vss_check_dm_info {
	uint32_t flags;
} __packed;

struct hv_vss_msg {
	union {
		struct hv_vss_hdr vss_hdr;
	} hdr;
	union {
		struct hv_vss_check_feature vss_cf;
		struct hv_vss_check_dm_info dm_info;
	} body;
} __packed;

struct hv_vss_req {
	struct hv_vss_opt_msg	opt_msg;	/* used to communicate with daemon */
	struct hv_vss_msg	msg;		/* used to communicate with host */
} __packed;

/* hv_vss debug control */
static int hv_vss_log = 0;

#define	hv_vss_log_error(...)	do {				\
	if (hv_vss_log > 0)					\
		log(LOG_ERR, "hv_vss: " __VA_ARGS__);		\
} while (0)

#define	hv_vss_log_info(...) do {				\
	if (hv_vss_log > 1)					\
		log(LOG_INFO, "hv_vss: " __VA_ARGS__);		\
} while (0)

static const struct vmbus_ic_desc vmbus_vss_descs[] = {
	{
		.ic_guid = { .hv_guid = {
		    0x29, 0x2e, 0xfa, 0x35, 0x23, 0xea, 0x36, 0x42,
		    0x96, 0xae, 0x3a, 0x6e, 0xba, 0xcb, 0xa4,  0x40} },
		.ic_desc = "Hyper-V VSS"
	},
	VMBUS_IC_DESC_END
};

static const char * vss_opt_name[] = {"None", "VSSCheck", "Freeze", "Thaw"};

/* character device prototypes */
static d_open_t		hv_vss_dev_open;
static d_close_t	hv_vss_dev_close;
static d_poll_t		hv_vss_dev_daemon_poll;
static d_ioctl_t	hv_vss_dev_daemon_ioctl;

static d_open_t		hv_appvss_dev_open;
static d_close_t	hv_appvss_dev_close;
static d_poll_t		hv_appvss_dev_poll;
static d_ioctl_t	hv_appvss_dev_ioctl;

/* hv_vss character device structure */
static struct cdevsw hv_vss_cdevsw =
{
	.d_version	= D_VERSION,
	.d_open		= hv_vss_dev_open,
	.d_close	= hv_vss_dev_close,
	.d_poll		= hv_vss_dev_daemon_poll,
	.d_ioctl	= hv_vss_dev_daemon_ioctl,
	.d_name		= FS_VSS_DEV_NAME,
};

static struct cdevsw hv_appvss_cdevsw =
{
	.d_version	= D_VERSION,
	.d_open		= hv_appvss_dev_open,
	.d_close	= hv_appvss_dev_close,
	.d_poll		= hv_appvss_dev_poll,
	.d_ioctl	= hv_appvss_dev_ioctl,
	.d_name		= APP_VSS_DEV_NAME,
};

struct hv_vss_sc;
/*
 * Global state to track cdev
 */
struct hv_vss_dev_sc {
	/*
	 * msg was transferred from host to notify queue, and
	 * ack queue. Finally, it was recyled to free list.
	 */
	STAILQ_HEAD(, hv_vss_req_internal) 	to_notify_queue;
	STAILQ_HEAD(, hv_vss_req_internal) 	to_ack_queue;
	struct hv_vss_sc			*sc;
	struct proc				*proc_task;
	struct selinfo				hv_vss_selinfo;
};
/*
 * Global state to track and synchronize the transaction requests from the host.
 * The VSS allows user to register their function to do freeze/thaw for application.
 * VSS kernel will notify both vss daemon and user application if it is registered.
 * The implementation state transition is illustrated by:
 * https://clovertrail.github.io/assets/vssdot.png
 */
typedef struct hv_vss_sc {
	struct vmbus_ic_softc			util_sc;
	device_t				dev;

	struct task				task;

	/*
	 * mutex is used to protect access of list/queue,
	 * callout in request is also used this mutex.
	 */
	struct mtx				pending_mutex;
	/*
	 * req_free_list contains all free items
	 */
	LIST_HEAD(, hv_vss_req_internal)	req_free_list;

	/* Indicates if daemon registered with driver */
	boolean_t				register_done;

	boolean_t				app_register_done;

	/* cdev for file system freeze/thaw */
	struct cdev				*hv_vss_dev;
	/* cdev for application freeze/thaw */
	struct cdev				*hv_appvss_dev;

	/* sc for app */
	struct hv_vss_dev_sc			app_sc;
	/* sc for deamon */
	struct hv_vss_dev_sc			daemon_sc;
} hv_vss_sc;

typedef struct hv_vss_req_internal {
	LIST_ENTRY(hv_vss_req_internal)		link;
	STAILQ_ENTRY(hv_vss_req_internal)	slink;
	struct hv_vss_req			vss_req;

	/* Rcv buffer for communicating with the host*/
	uint8_t					*rcv_buf;
	/* Length of host message */
	uint32_t				host_msg_len;
	/* Host message id */
	uint64_t				host_msg_id;

	hv_vss_sc				*sc;

	struct callout				callout;
} hv_vss_req_internal;

#define SEARCH_REMOVE_REQ_LOCKED(reqp, queue, link, tmp, id)		\
	do {								\
		STAILQ_FOREACH_SAFE(reqp, queue, link, tmp) {		\
			if (reqp->vss_req.opt_msg.msgid == id) {	\
				STAILQ_REMOVE(queue,			\
				    reqp, hv_vss_req_internal, link);	\
				break;					\
			}						\
		}							\
	} while (0)

static bool
hv_vss_is_daemon_killed_after_launch(hv_vss_sc *sc)
{
	return (!sc->register_done && sc->daemon_sc.proc_task);
}

/*
 * Callback routine that gets called whenever there is a message from host
 */
static void
hv_vss_callback(struct vmbus_channel *chan __unused, void *context)
{
	hv_vss_sc *sc = (hv_vss_sc*)context;
	if (hv_vss_is_daemon_killed_after_launch(sc))
		hv_vss_log_info("%s: daemon was killed!\n", __func__);
	if (sc->register_done || sc->daemon_sc.proc_task) {
		hv_vss_log_info("%s: Queuing work item\n", __func__);
		if (hv_vss_is_daemon_killed_after_launch(sc))
			hv_vss_log_info("%s: daemon was killed!\n", __func__);
		taskqueue_enqueue(taskqueue_thread, &sc->task);
	} else {
		hv_vss_log_info("%s: daemon has never been registered\n", __func__);
	}
	hv_vss_log_info("%s: received msg from host\n", __func__);
}
/*
 * Send the response back to the host.
 */
static void
hv_vss_respond_host(uint8_t *rcv_buf, struct vmbus_channel *ch,
    uint32_t recvlen, uint64_t requestid, uint32_t error)
{
	struct vmbus_icmsg_hdr *hv_icmsg_hdrp;

	hv_icmsg_hdrp = (struct vmbus_icmsg_hdr *)rcv_buf;

	hv_icmsg_hdrp->ic_status = error;
	hv_icmsg_hdrp->ic_flags = HV_ICMSGHDRFLAG_TRANSACTION | HV_ICMSGHDRFLAG_RESPONSE;

	error = vmbus_chan_send(ch, VMBUS_CHANPKT_TYPE_INBAND, 0,
	    rcv_buf, recvlen, requestid);
	if (error)
		hv_vss_log_info("%s: hv_vss_respond_host: sendpacket error:%d\n",
		    __func__, error);
}

static void
hv_vss_notify_host_result_locked(struct hv_vss_req_internal *reqp, uint32_t status)
{
	struct hv_vss_msg* msg = (struct hv_vss_msg *)reqp->rcv_buf;
	hv_vss_sc *sc = reqp->sc;
	if (reqp->vss_req.opt_msg.opt == HV_VSS_CHECK) {
		msg->body.vss_cf.flags = VSS_HBU_NO_AUTO_RECOVERY;
	}
	hv_vss_log_info("%s, %s response %s to host\n", __func__,
	    vss_opt_name[reqp->vss_req.opt_msg.opt],
	    status == HV_S_OK ? "Success" : "Fail");
	hv_vss_respond_host(reqp->rcv_buf, vmbus_get_channel(reqp->sc->dev),
	    reqp->host_msg_len, reqp->host_msg_id, status);
	/* recycle the request */
	LIST_INSERT_HEAD(&sc->req_free_list, reqp, link);
}

static void
hv_vss_notify_host_result(struct hv_vss_req_internal *reqp, uint32_t status)
{
	mtx_lock(&reqp->sc->pending_mutex);
	hv_vss_notify_host_result_locked(reqp, status);
	mtx_unlock(&reqp->sc->pending_mutex);
}

static void
hv_vss_cp_vssreq_to_user(struct hv_vss_req_internal *reqp,
    struct hv_vss_opt_msg *userdata)
{
	struct hv_vss_req *hv_vss_dev_buf;
	hv_vss_dev_buf = &reqp->vss_req;
	hv_vss_dev_buf->opt_msg.opt = HV_VSS_NONE;
	switch (reqp->vss_req.msg.hdr.vss_hdr.operation) {
	case VSS_OP_FREEZE:
		hv_vss_dev_buf->opt_msg.opt = HV_VSS_FREEZE;
		break;
	case VSS_OP_THAW:
		hv_vss_dev_buf->opt_msg.opt = HV_VSS_THAW;
		break;
	case VSS_OP_HOT_BACKUP:
		hv_vss_dev_buf->opt_msg.opt = HV_VSS_CHECK;
		break;
	}
	*userdata = hv_vss_dev_buf->opt_msg;
	hv_vss_log_info("%s, read data from user for "
	    "%s (%ju) \n", __func__, vss_opt_name[userdata->opt],
	    (uintmax_t)userdata->msgid);
}

/**
 * Remove the request id from app notifiy or ack queue,
 * and recyle the request by inserting it to free list.
 *
 * When app was notified but not yet sending ack, the request
 * should locate in either notify queue or ack queue.
 */
static struct hv_vss_req_internal*
hv_vss_drain_req_queue_locked(hv_vss_sc *sc, uint64_t req_id)
{
	struct hv_vss_req_internal *reqp, *tmp;
	SEARCH_REMOVE_REQ_LOCKED(reqp, &sc->daemon_sc.to_notify_queue,
	    slink, tmp, req_id);
	if (reqp == NULL)
		SEARCH_REMOVE_REQ_LOCKED(reqp, &sc->daemon_sc.to_ack_queue,
		    slink, tmp, req_id);
	if (reqp == NULL)
		SEARCH_REMOVE_REQ_LOCKED(reqp, &sc->app_sc.to_notify_queue,
		    slink, tmp, req_id);
	if (reqp == NULL)
		SEARCH_REMOVE_REQ_LOCKED(reqp, &sc->app_sc.to_ack_queue, slink,
		    tmp, req_id);
	return (reqp);
}
/**
 * Actions for daemon who has been notified.
 */
static void
hv_vss_notified(struct hv_vss_dev_sc *dev_sc, struct hv_vss_opt_msg *userdata)
{
	struct hv_vss_req_internal *reqp;
	mtx_lock(&dev_sc->sc->pending_mutex);
	if (!STAILQ_EMPTY(&dev_sc->to_notify_queue)) {
		reqp = STAILQ_FIRST(&dev_sc->to_notify_queue);
		hv_vss_cp_vssreq_to_user(reqp, userdata);
		STAILQ_REMOVE_HEAD(&dev_sc->to_notify_queue, slink);
		/* insert the msg to queue for write */
		STAILQ_INSERT_TAIL(&dev_sc->to_ack_queue, reqp, slink);
		userdata->status = VSS_SUCCESS;
	} else {
		/* Timeout occur, thus request was removed from queue. */
		hv_vss_log_info("%s: notify queue is empty!\n", __func__);
		userdata->status = VSS_FAIL;
	}
	mtx_unlock(&dev_sc->sc->pending_mutex);
}

static void
hv_vss_notify(struct hv_vss_dev_sc *dev_sc, struct hv_vss_req_internal *reqp)
{
	uint32_t opt = reqp->vss_req.opt_msg.opt;
	mtx_lock(&dev_sc->sc->pending_mutex);
	STAILQ_INSERT_TAIL(&dev_sc->to_notify_queue, reqp, slink);
	hv_vss_log_info("%s: issuing query %s (%ju) to %s\n", __func__,
	    vss_opt_name[opt], (uintmax_t)reqp->vss_req.opt_msg.msgid,
	    &dev_sc->sc->app_sc == dev_sc ? "app" : "daemon");
	mtx_unlock(&dev_sc->sc->pending_mutex);
	selwakeup(&dev_sc->hv_vss_selinfo);
}

/**
 * Actions for daemon who has acknowledged.
 */
static void
hv_vss_daemon_acked(struct hv_vss_dev_sc *dev_sc, struct hv_vss_opt_msg *userdata)
{
	struct hv_vss_req_internal	*reqp, *tmp;
	uint64_t			req_id;
	int				opt;
	uint32_t			status;

	opt = userdata->opt;
	req_id = userdata->msgid;
	status = userdata->status;
	/* make sure the reserved fields are all zeros. */
	memset(&userdata->reserved, 0, sizeof(struct hv_vss_opt_msg) -
	    __offsetof(struct hv_vss_opt_msg, reserved));
	mtx_lock(&dev_sc->sc->pending_mutex);
	SEARCH_REMOVE_REQ_LOCKED(reqp, &dev_sc->to_ack_queue, slink, tmp, req_id);
	mtx_unlock(&dev_sc->sc->pending_mutex);
	if (reqp == NULL) {
		hv_vss_log_info("%s Timeout: fail to find daemon ack request\n",
		    __func__);
		userdata->status = VSS_FAIL;
		return;
	}
	KASSERT(opt == reqp->vss_req.opt_msg.opt, ("Mismatched VSS operation!"));
	hv_vss_log_info("%s, get response %d from daemon for %s (%ju) \n", __func__,
	    status, vss_opt_name[opt], (uintmax_t)req_id);
	switch (opt) {
	case HV_VSS_CHECK:
	case HV_VSS_FREEZE:
		callout_drain(&reqp->callout);
		hv_vss_notify_host_result(reqp,
		    status == VSS_SUCCESS ? HV_S_OK : HV_E_FAIL);
		break;
	case HV_VSS_THAW:
		if (dev_sc->sc->app_register_done) {
			if (status == VSS_SUCCESS) {
				hv_vss_notify(&dev_sc->sc->app_sc, reqp);
			} else {
				/* handle error */
				callout_drain(&reqp->callout);
				hv_vss_notify_host_result(reqp, HV_E_FAIL);
			}
		} else {
			callout_drain(&reqp->callout);
			hv_vss_notify_host_result(reqp,
			    status == VSS_SUCCESS ? HV_S_OK : HV_E_FAIL);
		}
		break;
	}
}

/**
 * Actions for app who has acknowledged.
 */
static void
hv_vss_app_acked(struct hv_vss_dev_sc *dev_sc, struct hv_vss_opt_msg *userdata)
{
	struct hv_vss_req_internal	*reqp, *tmp;
	uint64_t			req_id;
	int				opt;
	uint8_t				status;

	opt = userdata->opt;
	req_id = userdata->msgid;
	status = userdata->status;
	/* make sure the reserved fields are all zeros. */
	memset(&userdata->reserved, 0, sizeof(struct hv_vss_opt_msg) -
	    __offsetof(struct hv_vss_opt_msg, reserved));
	mtx_lock(&dev_sc->sc->pending_mutex);
	SEARCH_REMOVE_REQ_LOCKED(reqp, &dev_sc->to_ack_queue, slink, tmp, req_id);
	mtx_unlock(&dev_sc->sc->pending_mutex);
	if (reqp == NULL) {
		hv_vss_log_info("%s Timeout: fail to find app ack request\n",
		    __func__);
		userdata->status = VSS_FAIL;
		return;
	}
	KASSERT(opt == reqp->vss_req.opt_msg.opt, ("Mismatched VSS operation!"));
	hv_vss_log_info("%s, get response %d from app for %s (%ju) \n",
	    __func__, status, vss_opt_name[opt], (uintmax_t)req_id);
	if (dev_sc->sc->register_done) {
		switch (opt) {
		case HV_VSS_CHECK:
		case HV_VSS_FREEZE:
			if (status == VSS_SUCCESS) {
				hv_vss_notify(&dev_sc->sc->daemon_sc, reqp);
			} else {
				/* handle error */
				callout_drain(&reqp->callout);
				hv_vss_notify_host_result(reqp, HV_E_FAIL);
			}
			break;
		case HV_VSS_THAW:
			callout_drain(&reqp->callout);
			hv_vss_notify_host_result(reqp,
			    status == VSS_SUCCESS ? HV_S_OK : HV_E_FAIL);
			break;
		}
	} else {
		hv_vss_log_info("%s, Fatal: vss daemon was killed\n", __func__);
	}
}

static int
hv_vss_dev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct proc     *td_proc;
	td_proc = td->td_proc;

	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;
	hv_vss_log_info("%s: %s opens device \"%s\" successfully.\n",
	    __func__, td_proc->p_comm, FS_VSS_DEV_NAME);

	if (dev_sc->sc->register_done)
		return (EBUSY);

	dev_sc->sc->register_done = true;
	hv_vss_callback(vmbus_get_channel(dev_sc->sc->dev), dev_sc->sc);

	dev_sc->proc_task = curproc;
	return (0);
}

static int
hv_vss_dev_close(struct cdev *dev, int fflag __unused, int devtype __unused,
				 struct thread *td)
{
	struct proc     *td_proc;
	td_proc = td->td_proc;

	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	hv_vss_log_info("%s: %s closes device \"%s\"\n",
	    __func__, td_proc->p_comm, FS_VSS_DEV_NAME);
	dev_sc->sc->register_done = false;
	return (0);
}

static int
hv_vss_dev_daemon_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct proc			*td_proc;
	struct hv_vss_dev_sc		*sc;

	td_proc = td->td_proc;
	sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	hv_vss_log_info("%s: %s invoked vss ioctl\n", __func__, td_proc->p_comm);

	struct hv_vss_opt_msg* userdata = (struct hv_vss_opt_msg*)data;
	switch(cmd) {
	case IOCHVVSSREAD:
		hv_vss_notified(sc, userdata);
		break;
	case IOCHVVSSWRITE:
		hv_vss_daemon_acked(sc, userdata);
		break;
	}
	return (0);
}

/*
 * hv_vss_daemon poll invokes this function to check if data is available
 * for daemon to read.
 */
static int
hv_vss_dev_daemon_poll(struct cdev *dev, int events, struct thread *td)
{
	int revent = 0;
	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	mtx_lock(&dev_sc->sc->pending_mutex);
	/**
	 * if there is data ready, inform daemon's poll
	 */
	if (!STAILQ_EMPTY(&dev_sc->to_notify_queue))
		revent = POLLIN;
	if (revent == 0)
		selrecord(td, &dev_sc->hv_vss_selinfo);
	hv_vss_log_info("%s return 0x%x\n", __func__, revent);
	mtx_unlock(&dev_sc->sc->pending_mutex);
	return (revent);
}

static int
hv_appvss_dev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct proc     *td_proc;
	td_proc = td->td_proc;

	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;
	hv_vss_log_info("%s: %s opens device \"%s\" successfully.\n",
	    __func__, td_proc->p_comm, APP_VSS_DEV_NAME);

	if (dev_sc->sc->app_register_done)
		return (EBUSY);

	dev_sc->sc->app_register_done = true;
	dev_sc->proc_task = curproc;
	return (0);
}

static int
hv_appvss_dev_close(struct cdev *dev, int fflag __unused, int devtype __unused,
				 struct thread *td)
{
	struct proc     *td_proc;
	td_proc = td->td_proc;

	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	hv_vss_log_info("%s: %s closes device \"%s\".\n",
	    __func__, td_proc->p_comm, APP_VSS_DEV_NAME);
	dev_sc->sc->app_register_done = false;
	return (0);
}

static int
hv_appvss_dev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag,
    struct thread *td)
{
	struct proc			*td_proc;
	struct hv_vss_dev_sc		*dev_sc;

	td_proc = td->td_proc;
	dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	hv_vss_log_info("%s: %s invoked vss ioctl\n", __func__, td_proc->p_comm);

	struct hv_vss_opt_msg* userdata = (struct hv_vss_opt_msg*)data;
	switch(cmd) {
	case IOCHVVSSREAD:
		hv_vss_notified(dev_sc, userdata);
		break;
	case IOCHVVSSWRITE:
		hv_vss_app_acked(dev_sc, userdata);
		break;
	}
	return (0);
}

/*
 * hv_vss_daemon poll invokes this function to check if data is available
 * for daemon to read.
 */
static int
hv_appvss_dev_poll(struct cdev *dev, int events, struct thread *td)
{
	int revent = 0;
	struct hv_vss_dev_sc *dev_sc = (struct hv_vss_dev_sc*)dev->si_drv1;

	mtx_lock(&dev_sc->sc->pending_mutex);
	/**
	 * if there is data ready, inform daemon's poll
	 */
	if (!STAILQ_EMPTY(&dev_sc->to_notify_queue))
		revent = POLLIN;
	if (revent == 0)
		selrecord(td, &dev_sc->hv_vss_selinfo);
	hv_vss_log_info("%s return 0x%x\n", __func__, revent);
	mtx_unlock(&dev_sc->sc->pending_mutex);
	return (revent);
}

static void
hv_vss_timeout(void *arg)
{
	hv_vss_req_internal *reqp = arg;
	hv_vss_req_internal *request;
	hv_vss_sc* sc = reqp->sc;
	uint64_t req_id = reqp->vss_req.opt_msg.msgid;
	/* This thread is locked */
	KASSERT(mtx_owned(&sc->pending_mutex), ("mutex lock is not owned!"));
	request = hv_vss_drain_req_queue_locked(sc, req_id);
	KASSERT(request != NULL, ("timeout but fail to find request"));
	hv_vss_notify_host_result_locked(reqp, HV_E_FAIL);
}

/*
 * This routine is called whenever a message is received from the host
 */
static void
hv_vss_init_req(hv_vss_req_internal *reqp,
    uint32_t recvlen, uint64_t requestid, uint8_t *vss_buf, hv_vss_sc *sc)
{
	struct timespec vm_ts;
	struct hv_vss_msg* msg = (struct hv_vss_msg *)vss_buf;

	memset(reqp, 0, __offsetof(hv_vss_req_internal, callout));
	reqp->host_msg_len = recvlen;
	reqp->host_msg_id = requestid;
	reqp->rcv_buf = vss_buf;
	reqp->sc = sc;
	memcpy(&reqp->vss_req.msg,
	    (struct hv_vss_msg *)vss_buf, sizeof(struct hv_vss_msg));
	/* set the opt for users */
	switch (msg->hdr.vss_hdr.operation) {
	case VSS_OP_FREEZE:
		reqp->vss_req.opt_msg.opt = HV_VSS_FREEZE;
		break;
	case VSS_OP_THAW:
		reqp->vss_req.opt_msg.opt = HV_VSS_THAW;
		break;
	case VSS_OP_HOT_BACKUP:
		reqp->vss_req.opt_msg.opt = HV_VSS_CHECK;
		break;
	}
	/* Use a timestamp as msg request ID */
	nanotime(&vm_ts);
	reqp->vss_req.opt_msg.msgid = (vm_ts.tv_sec * NANOSEC) + vm_ts.tv_nsec;
}

static hv_vss_req_internal*
hv_vss_get_new_req_locked(hv_vss_sc *sc)
{
	hv_vss_req_internal *reqp;
	if (!STAILQ_EMPTY(&sc->daemon_sc.to_notify_queue) ||
	    !STAILQ_EMPTY(&sc->daemon_sc.to_ack_queue) ||
	    !STAILQ_EMPTY(&sc->app_sc.to_notify_queue) ||
	    !STAILQ_EMPTY(&sc->app_sc.to_ack_queue)) {
		/*
		 * There is request coming from host before
		 * finishing previous requests
		 */
		hv_vss_log_info("%s: Warning: there is new request "
		    "coming before finishing previous requests\n", __func__);
		return (NULL);
	}
	if (LIST_EMPTY(&sc->req_free_list)) {
		/* TODO Error: no buffer */
		hv_vss_log_info("Error: No buffer\n");
		return (NULL);
	}
	reqp = LIST_FIRST(&sc->req_free_list);
	LIST_REMOVE(reqp, link);
	return (reqp);
}

static void
hv_vss_start_notify(hv_vss_req_internal *reqp, uint32_t opt)
{
	hv_vss_sc *sc = reqp->sc;
	/*
	 * Freeze/Check notification sequence: kernel -> app -> daemon(fs)
	 * Thaw notification sequence:         kernel -> daemon(fs) -> app
	 *
	 * We should wake up the daemon, in case it's doing poll().
	 * The response should be received after 5s, otherwise, trigger timeout.
	 */
	switch (opt) {
	case VSS_OP_FREEZE:
	case VSS_OP_HOT_BACKUP:
		if (sc->app_register_done)
			hv_vss_notify(&sc->app_sc, reqp);
		else
			hv_vss_notify(&sc->daemon_sc, reqp);
		callout_reset(&reqp->callout, TIMEOUT_LIMIT * hz,
		    hv_vss_timeout, reqp);
		break;
	case VSS_OP_THAW:
		hv_vss_notify(&sc->daemon_sc, reqp);
		callout_reset(&reqp->callout, TIMEOUT_LIMIT * hz,
		    hv_vss_timeout, reqp);
		break;
	}
}

/*
 * Function to read the vss request buffer from host
 * and interact with daemon
 */
static void
hv_vss_process_request(void *context, int pending __unused)
{
	uint8_t *vss_buf;
	struct vmbus_channel *channel;
	uint32_t recvlen = 0;
	uint64_t requestid;
	struct vmbus_icmsg_hdr *icmsghdrp;
	int ret = 0;
	hv_vss_sc *sc;
	hv_vss_req_internal *reqp;

	hv_vss_log_info("%s: entering hv_vss_process_request\n", __func__);

	sc = (hv_vss_sc*)context;
	vss_buf = sc->util_sc.ic_buf;
	channel = vmbus_get_channel(sc->dev);

	recvlen = sc->util_sc.ic_buflen;
	ret = vmbus_chan_recv(channel, vss_buf, &recvlen, &requestid);
	KASSERT(ret != ENOBUFS, ("hvvss recvbuf is not large enough"));
	/* XXX check recvlen to make sure that it contains enough data */

	while ((ret == 0) && (recvlen > 0)) {
		icmsghdrp = (struct vmbus_icmsg_hdr *)vss_buf;

		if (icmsghdrp->ic_type == HV_ICMSGTYPE_NEGOTIATE) {
			ret = vmbus_ic_negomsg(&sc->util_sc, vss_buf,
			    &recvlen, VSS_FWVER, VSS_MSGVER);
			hv_vss_respond_host(vss_buf, vmbus_get_channel(sc->dev),
			    recvlen, requestid, ret);
			hv_vss_log_info("%s: version negotiated\n", __func__);
		} else if (!hv_vss_is_daemon_killed_after_launch(sc)) {
			struct hv_vss_msg* msg = (struct hv_vss_msg *)vss_buf;
			switch(msg->hdr.vss_hdr.operation) {
			case VSS_OP_FREEZE:
			case VSS_OP_THAW:
			case VSS_OP_HOT_BACKUP:
				mtx_lock(&sc->pending_mutex);
				reqp = hv_vss_get_new_req_locked(sc);
				mtx_unlock(&sc->pending_mutex);
				if (reqp == NULL) {
					/* ignore this request from host */
					break;
				}
				hv_vss_init_req(reqp, recvlen, requestid, vss_buf, sc);
				hv_vss_log_info("%s: receive %s (%ju) from host\n",
				    __func__,
				    vss_opt_name[reqp->vss_req.opt_msg.opt],
				    (uintmax_t)reqp->vss_req.opt_msg.msgid);
				hv_vss_start_notify(reqp, msg->hdr.vss_hdr.operation);
				break;
			case VSS_OP_GET_DM_INFO:
				hv_vss_log_info("%s: receive GET_DM_INFO from host\n",
				    __func__);
				msg->body.dm_info.flags = 0;
				hv_vss_respond_host(vss_buf, vmbus_get_channel(sc->dev),
				    recvlen, requestid, HV_S_OK);
				break;
			default:
				device_printf(sc->dev, "Unknown opt from host: %d\n",
				    msg->hdr.vss_hdr.operation);
				break;
			}
		} else {
			/* daemon was killed for some reason after it was launched */
			struct hv_vss_msg* msg = (struct hv_vss_msg *)vss_buf;
			switch(msg->hdr.vss_hdr.operation) {
			case VSS_OP_FREEZE:
				hv_vss_log_info("%s: response fail for FREEZE\n",
				    __func__);
				break;
			case VSS_OP_THAW:
				hv_vss_log_info("%s: response fail for THAW\n",
				    __func__);
				break;
			case VSS_OP_HOT_BACKUP:
				hv_vss_log_info("%s: response fail for HOT_BACKUP\n",
				    __func__);
				msg->body.vss_cf.flags = VSS_HBU_NO_AUTO_RECOVERY;
				break;
			case VSS_OP_GET_DM_INFO:
				hv_vss_log_info("%s: response fail for GET_DM_INFO\n",
				    __func__);
				msg->body.dm_info.flags = 0;
				break;
			default:
				device_printf(sc->dev, "Unknown opt from host: %d\n",
				    msg->hdr.vss_hdr.operation);
				break;
			}
			hv_vss_respond_host(vss_buf, vmbus_get_channel(sc->dev),
			    recvlen, requestid, HV_E_FAIL);
		}
		/*
		 * Try reading next buffer
		 */
		recvlen = sc->util_sc.ic_buflen;
		ret = vmbus_chan_recv(channel, vss_buf, &recvlen, &requestid);
		KASSERT(ret != ENOBUFS, ("hvvss recvbuf is not large enough"));
		/* XXX check recvlen to make sure that it contains enough data */

		hv_vss_log_info("%s: read: context %p, ret =%d, recvlen=%d\n",
		    __func__, context, ret, recvlen);
	}
}

static int
hv_vss_probe(device_t dev)
{
	return (vmbus_ic_probe(dev, vmbus_vss_descs));
}

static int
hv_vss_init_send_receive_queue(device_t dev)
{
	hv_vss_sc *sc = (hv_vss_sc*)device_get_softc(dev);
	int i;
	const int max_list = 4; /* It is big enough for the list */
	struct hv_vss_req_internal* reqp;

	LIST_INIT(&sc->req_free_list);
	STAILQ_INIT(&sc->daemon_sc.to_notify_queue);
	STAILQ_INIT(&sc->daemon_sc.to_ack_queue);
	STAILQ_INIT(&sc->app_sc.to_notify_queue);
	STAILQ_INIT(&sc->app_sc.to_ack_queue);

	for (i = 0; i < max_list; i++) {
		reqp = malloc(sizeof(struct hv_vss_req_internal),
		    M_DEVBUF, M_WAITOK|M_ZERO);
		LIST_INSERT_HEAD(&sc->req_free_list, reqp, link);
		callout_init_mtx(&reqp->callout, &sc->pending_mutex, 0);
	}
	return (0);
}

static int
hv_vss_destroy_send_receive_queue(device_t dev)
{
	hv_vss_sc *sc = (hv_vss_sc*)device_get_softc(dev);
	hv_vss_req_internal* reqp;

	while (!LIST_EMPTY(&sc->req_free_list)) {
		reqp = LIST_FIRST(&sc->req_free_list);
		LIST_REMOVE(reqp, link);
		free(reqp, M_DEVBUF);
	}

	while (!STAILQ_EMPTY(&sc->daemon_sc.to_notify_queue)) {
		reqp = STAILQ_FIRST(&sc->daemon_sc.to_notify_queue);
		STAILQ_REMOVE_HEAD(&sc->daemon_sc.to_notify_queue, slink);
		free(reqp, M_DEVBUF);
	}

	while (!STAILQ_EMPTY(&sc->daemon_sc.to_ack_queue)) {
		reqp = STAILQ_FIRST(&sc->daemon_sc.to_ack_queue);
		STAILQ_REMOVE_HEAD(&sc->daemon_sc.to_ack_queue, slink);
		free(reqp, M_DEVBUF);
	}

	while (!STAILQ_EMPTY(&sc->app_sc.to_notify_queue)) {
		reqp = STAILQ_FIRST(&sc->app_sc.to_notify_queue);
		STAILQ_REMOVE_HEAD(&sc->app_sc.to_notify_queue, slink);
		free(reqp, M_DEVBUF);
	}

	while (!STAILQ_EMPTY(&sc->app_sc.to_ack_queue)) {
		reqp = STAILQ_FIRST(&sc->app_sc.to_ack_queue);
		STAILQ_REMOVE_HEAD(&sc->app_sc.to_ack_queue, slink);
		free(reqp, M_DEVBUF);
	}
	return (0);
}

static int
hv_vss_attach(device_t dev)
{
	int error;
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;

	hv_vss_sc *sc = (hv_vss_sc*)device_get_softc(dev);

	sc->dev = dev;
	mtx_init(&sc->pending_mutex, "hv_vss pending mutex", NULL, MTX_DEF);

	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "hv_vss_log",
	    CTLFLAG_RWTUN, &hv_vss_log, 0, "Hyperv VSS service log level");

	TASK_INIT(&sc->task, 0, hv_vss_process_request, sc);
	hv_vss_init_send_receive_queue(dev);
	/* create character device for file system freeze/thaw */
	error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		    &sc->hv_vss_dev,
		    &hv_vss_cdevsw,
		    0,
		    UID_ROOT,
		    GID_WHEEL,
		    0640,
		    FS_VSS_DEV_NAME);

	if (error != 0) {
		hv_vss_log_info("Fail to create '%s': %d\n", FS_VSS_DEV_NAME, error);
		return (error);
	}
	sc->hv_vss_dev->si_drv1 = &sc->daemon_sc;
	sc->daemon_sc.sc = sc;
	/* create character device for application freeze/thaw */
	error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
		    &sc->hv_appvss_dev,
		    &hv_appvss_cdevsw,
		    0,
		    UID_ROOT,
		    GID_WHEEL,
		    0640,
		    APP_VSS_DEV_NAME);

	if (error != 0) {
		hv_vss_log_info("Fail to create '%s': %d\n", APP_VSS_DEV_NAME, error);
		return (error);
	}
	sc->hv_appvss_dev->si_drv1 = &sc->app_sc;
	sc->app_sc.sc = sc;

	return (vmbus_ic_attach(dev, hv_vss_callback));
}

static int
hv_vss_detach(device_t dev)
{
	hv_vss_sc *sc = (hv_vss_sc*)device_get_softc(dev);
	mtx_destroy(&sc->pending_mutex);
	if (sc->daemon_sc.proc_task != NULL) {
		PROC_LOCK(sc->daemon_sc.proc_task);
		kern_psignal(sc->daemon_sc.proc_task, SIGKILL);
		PROC_UNLOCK(sc->daemon_sc.proc_task);
	}
	if (sc->app_sc.proc_task != NULL) {
		PROC_LOCK(sc->app_sc.proc_task);
		kern_psignal(sc->app_sc.proc_task, SIGKILL);
		PROC_UNLOCK(sc->app_sc.proc_task);
	}
	hv_vss_destroy_send_receive_queue(dev);
	destroy_dev(sc->hv_vss_dev);
	destroy_dev(sc->hv_appvss_dev);
	return (vmbus_ic_detach(dev));
}

static device_method_t vss_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, hv_vss_probe),
	DEVMETHOD(device_attach, hv_vss_attach),
	DEVMETHOD(device_detach, hv_vss_detach),
	{ 0, 0 }
};

static driver_t vss_driver = { "hvvss", vss_methods, sizeof(hv_vss_sc)};

static devclass_t vss_devclass;

DRIVER_MODULE(hv_vss, vmbus, vss_driver, vss_devclass, NULL, NULL);
MODULE_VERSION(hv_vss, 1);
MODULE_DEPEND(hv_vss, vmbus, 1, 1, 1);
