/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/poll.h>
#include <sys/reboot.h>
#include <sys/rman.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/watchdog.h>

#ifdef LOCAL_MODULE
#include <ipmi.h>
#include <ipmivars.h>
#else
#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>
#endif

/*
 * Driver request structures are allocated on the stack via alloca() to
 * avoid calling malloc(), especially for the watchdog handler.
 * To avoid too much stack growth, a previously allocated structure can
 * be reused via IPMI_INIT_DRIVER_REQUEST(), but the caller should ensure
 * that there is adequate reply/request space in the original allocation.
 */
#define	IPMI_INIT_DRIVER_REQUEST(req, addr, cmd, reqlen, replylen)	\
	bzero((req), sizeof(struct ipmi_request));			\
	ipmi_init_request((req), NULL, 0, (addr), (cmd), (reqlen), (replylen))

#define	IPMI_ALLOC_DRIVER_REQUEST(req, addr, cmd, reqlen, replylen)	\
	(req) = __builtin_alloca(sizeof(struct ipmi_request) +		\
	    (reqlen) + (replylen));					\
	IPMI_INIT_DRIVER_REQUEST((req), (addr), (cmd), (reqlen),	\
	    (replylen))

#ifdef IPMB
static int ipmi_ipmb_checksum(u_char, int);
static int ipmi_ipmb_send_message(device_t, u_char, u_char, u_char,
     u_char, u_char, int)
#endif

static d_ioctl_t ipmi_ioctl;
static d_poll_t ipmi_poll;
static d_open_t ipmi_open;
static void ipmi_dtor(void *arg);

int ipmi_attached = 0;

static int on = 1;
static bool wd_in_shutdown = false;
static int wd_timer_actions = IPMI_SET_WD_ACTION_POWER_CYCLE;
static int wd_shutdown_countdown = 0; /* sec */
static int wd_startup_countdown = 0; /* sec */
static int wd_pretimeout_countdown = 120; /* sec */
static int cycle_wait = 10; /* sec */

static SYSCTL_NODE(_hw, OID_AUTO, ipmi, CTLFLAG_RD, 0,
    "IPMI driver parameters");
SYSCTL_INT(_hw_ipmi, OID_AUTO, on, CTLFLAG_RWTUN,
	&on, 0, "");
SYSCTL_INT(_hw_ipmi, OID_AUTO, wd_timer_actions, CTLFLAG_RW,
	&wd_timer_actions, 0,
	"IPMI watchdog timer actions (including pre-timeout interrupt)");
SYSCTL_INT(_hw_ipmi, OID_AUTO, wd_shutdown_countdown, CTLFLAG_RW,
	&wd_shutdown_countdown, 0,
	"IPMI watchdog countdown for shutdown (seconds)");
SYSCTL_INT(_hw_ipmi, OID_AUTO, wd_startup_countdown, CTLFLAG_RDTUN,
	&wd_startup_countdown, 0,
	"IPMI watchdog countdown initialized during startup (seconds)");
SYSCTL_INT(_hw_ipmi, OID_AUTO, wd_pretimeout_countdown, CTLFLAG_RW,
	&wd_pretimeout_countdown, 0,
	"IPMI watchdog pre-timeout countdown (seconds)");
SYSCTL_INT(_hw_ipmi, OID_AUTO, cyle_wait, CTLFLAG_RWTUN,
	&cycle_wait, 0,
	"IPMI power cycle on reboot delay time (seconds)");

static struct cdevsw ipmi_cdevsw = {
	.d_version =    D_VERSION,
	.d_open =	ipmi_open,
	.d_ioctl =	ipmi_ioctl,
	.d_poll =	ipmi_poll,
	.d_name =	"ipmi",
};

static MALLOC_DEFINE(M_IPMI, "ipmi", "ipmi");

static int
ipmi_open(struct cdev *cdev, int flags, int fmt, struct thread *td)
{
	struct ipmi_device *dev;
	struct ipmi_softc *sc;
	int error;

	if (!on)
		return (ENOENT);

	/* Initialize the per file descriptor data. */
	dev = malloc(sizeof(struct ipmi_device), M_IPMI, M_WAITOK | M_ZERO);
	error = devfs_set_cdevpriv(dev, ipmi_dtor);
	if (error) {
		free(dev, M_IPMI);
		return (error);
	}

	sc = cdev->si_drv1;
	TAILQ_INIT(&dev->ipmi_completed_requests);
	dev->ipmi_address = IPMI_BMC_SLAVE_ADDR;
	dev->ipmi_lun = IPMI_BMC_SMS_LUN;
	dev->ipmi_softc = sc;
	IPMI_LOCK(sc);
	sc->ipmi_opened++;
	IPMI_UNLOCK(sc);

	return (0);
}

static int
ipmi_poll(struct cdev *cdev, int poll_events, struct thread *td)
{
	struct ipmi_device *dev;
	struct ipmi_softc *sc;
	int revents = 0;

	if (devfs_get_cdevpriv((void **)&dev))
		return (0);

	sc = cdev->si_drv1;
	IPMI_LOCK(sc);
	if (poll_events & (POLLIN | POLLRDNORM)) {
		if (!TAILQ_EMPTY(&dev->ipmi_completed_requests))
		    revents |= poll_events & (POLLIN | POLLRDNORM);
		if (dev->ipmi_requests == 0)
		    revents |= POLLERR;
	}

	if (revents == 0) {
		if (poll_events & (POLLIN | POLLRDNORM))
			selrecord(td, &dev->ipmi_select);
	}
	IPMI_UNLOCK(sc);

	return (revents);
}

static void
ipmi_purge_completed_requests(struct ipmi_device *dev)
{
	struct ipmi_request *req;

	while (!TAILQ_EMPTY(&dev->ipmi_completed_requests)) {
		req = TAILQ_FIRST(&dev->ipmi_completed_requests);
		TAILQ_REMOVE(&dev->ipmi_completed_requests, req, ir_link);
		dev->ipmi_requests--;
		ipmi_free_request(req);
	}
}

static void
ipmi_dtor(void *arg)
{
	struct ipmi_request *req, *nreq;
	struct ipmi_device *dev;
	struct ipmi_softc *sc;

	dev = arg;
	sc = dev->ipmi_softc;

	IPMI_LOCK(sc);
	if (dev->ipmi_requests) {
		/* Throw away any pending requests for this device. */
		TAILQ_FOREACH_SAFE(req, &sc->ipmi_pending_requests, ir_link,
		    nreq) {
			if (req->ir_owner == dev) {
				TAILQ_REMOVE(&sc->ipmi_pending_requests, req,
				    ir_link);
				dev->ipmi_requests--;
				ipmi_free_request(req);
			}
		}

		/* Throw away any pending completed requests for this device. */
		ipmi_purge_completed_requests(dev);

		/*
		 * If we still have outstanding requests, they must be stuck
		 * in an interface driver, so wait for those to drain.
		 */
		dev->ipmi_closing = 1;
		while (dev->ipmi_requests > 0) {
			msleep(&dev->ipmi_requests, &sc->ipmi_requests_lock,
			    PWAIT, "ipmidrain", 0);
			ipmi_purge_completed_requests(dev);
		}
	}
	sc->ipmi_opened--;
	IPMI_UNLOCK(sc);

	/* Cleanup. */
	free(dev, M_IPMI);
}

#ifdef IPMB
static int
ipmi_ipmb_checksum(u_char *data, int len)
{
	u_char sum = 0;

	for (; len; len--) {
		sum += *data++;
	}
	return (-sum);
}

/* XXX: Needs work */
static int
ipmi_ipmb_send_message(device_t dev, u_char channel, u_char netfn,
    u_char command, u_char seq, u_char *data, int data_len)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	struct ipmi_request *req;
	u_char slave_addr = 0x52;
	int error;

	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_SEND_MSG, data_len + 8, 0);
	req->ir_request[0] = channel;
	req->ir_request[1] = slave_addr;
	req->ir_request[2] = IPMI_ADDR(netfn, 0);
	req->ir_request[3] = ipmi_ipmb_checksum(&req->ir_request[1], 2);
	req->ir_request[4] = sc->ipmi_address;
	req->ir_request[5] = IPMI_ADDR(seq, sc->ipmi_lun);
	req->ir_request[6] = command;

	bcopy(data, &req->ir_request[7], data_len);
	temp[data_len + 7] = ipmi_ipmb_checksum(&req->ir_request[4],
	    data_len + 3);

	ipmi_submit_driver_request(sc, req);
	error = req->ir_error;

	return (error);
}

static int
ipmi_handle_attn(struct ipmi_softc *sc)
{
	struct ipmi_request *req;
	int error;

	device_printf(sc->ipmi_dev, "BMC has a message\n");
	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_MSG_FLAGS, 0, 1);

	ipmi_submit_driver_request(sc, req);

	if (req->ir_error == 0 && req->ir_compcode == 0) {
		if (req->ir_reply[0] & IPMI_MSG_BUFFER_FULL) {
			device_printf(sc->ipmi_dev, "message buffer full");
		}
		if (req->ir_reply[0] & IPMI_WDT_PRE_TIMEOUT) {
			device_printf(sc->ipmi_dev,
			    "watchdog about to go off");
		}
		if (req->ir_reply[0] & IPMI_MSG_AVAILABLE) {
			IPMI_ALLOC_DRIVER_REQUEST(req,
			    IPMI_ADDR(IPMI_APP_REQUEST, 0), IPMI_GET_MSG, 0,
			    16);

			device_printf(sc->ipmi_dev, "throw out message ");
			dump_buf(temp, 16);
		}
	}
	error = req->ir_error;

	return (error);
}
#endif

#ifdef IPMICTL_SEND_COMMAND_32
#define	PTRIN(p)	((void *)(uintptr_t)(p))
#define	PTROUT(p)	((uintptr_t)(p))
#endif

static int
ipmi_ioctl(struct cdev *cdev, u_long cmd, caddr_t data,
    int flags, struct thread *td)
{
	struct ipmi_softc *sc;
	struct ipmi_device *dev;
	struct ipmi_request *kreq;
	struct ipmi_req *req = (struct ipmi_req *)data;
	struct ipmi_recv *recv = (struct ipmi_recv *)data;
	struct ipmi_addr addr;
#ifdef IPMICTL_SEND_COMMAND_32
	struct ipmi_req32 *req32 = (struct ipmi_req32 *)data;
	struct ipmi_recv32 *recv32 = (struct ipmi_recv32 *)data;
	union {
		struct ipmi_req req;
		struct ipmi_recv recv;
	} thunk32;
#endif
	int error, len;

	error = devfs_get_cdevpriv((void **)&dev);
	if (error)
		return (error);

	sc = cdev->si_drv1;

#ifdef IPMICTL_SEND_COMMAND_32
	/* Convert 32-bit structures to native. */
	switch (cmd) {
	case IPMICTL_SEND_COMMAND_32:
		req = &thunk32.req;
		req->addr = PTRIN(req32->addr);
		req->addr_len = req32->addr_len;
		req->msgid = req32->msgid;
		req->msg.netfn = req32->msg.netfn;
		req->msg.cmd = req32->msg.cmd;
		req->msg.data_len = req32->msg.data_len;
		req->msg.data = PTRIN(req32->msg.data);
		break;
	case IPMICTL_RECEIVE_MSG_TRUNC_32:
	case IPMICTL_RECEIVE_MSG_32:
		recv = &thunk32.recv;
		recv->addr = PTRIN(recv32->addr);
		recv->addr_len = recv32->addr_len;
		recv->msg.data_len = recv32->msg.data_len;
		recv->msg.data = PTRIN(recv32->msg.data);
		break;
	}
#endif

	switch (cmd) {
#ifdef IPMICTL_SEND_COMMAND_32
	case IPMICTL_SEND_COMMAND_32:
#endif
	case IPMICTL_SEND_COMMAND:
		/*
		 * XXX: Need to add proper handling of this.
		 */
		error = copyin(req->addr, &addr, sizeof(addr));
		if (error)
			return (error);

		IPMI_LOCK(sc);
		/* clear out old stuff in queue of stuff done */
		/* XXX: This seems odd. */
		while ((kreq = TAILQ_FIRST(&dev->ipmi_completed_requests))) {
			TAILQ_REMOVE(&dev->ipmi_completed_requests, kreq,
			    ir_link);
			dev->ipmi_requests--;
			ipmi_free_request(kreq);
		}
		IPMI_UNLOCK(sc);

		kreq = ipmi_alloc_request(dev, req->msgid,
		    IPMI_ADDR(req->msg.netfn, 0), req->msg.cmd,
		    req->msg.data_len, IPMI_MAX_RX);
		error = copyin(req->msg.data, kreq->ir_request,
		    req->msg.data_len);
		if (error) {
			ipmi_free_request(kreq);
			return (error);
		}
		IPMI_LOCK(sc);
		dev->ipmi_requests++;
		error = sc->ipmi_enqueue_request(sc, kreq);
		IPMI_UNLOCK(sc);
		if (error)
			return (error);
		break;
#ifdef IPMICTL_SEND_COMMAND_32
	case IPMICTL_RECEIVE_MSG_TRUNC_32:
	case IPMICTL_RECEIVE_MSG_32:
#endif
	case IPMICTL_RECEIVE_MSG_TRUNC:
	case IPMICTL_RECEIVE_MSG:
		error = copyin(recv->addr, &addr, sizeof(addr));
		if (error)
			return (error);

		IPMI_LOCK(sc);
		kreq = TAILQ_FIRST(&dev->ipmi_completed_requests);
		if (kreq == NULL) {
			IPMI_UNLOCK(sc);
			return (EAGAIN);
		}
		addr.channel = IPMI_BMC_CHANNEL;
		/* XXX */
		recv->recv_type = IPMI_RESPONSE_RECV_TYPE;
		recv->msgid = kreq->ir_msgid;
		recv->msg.netfn = IPMI_REPLY_ADDR(kreq->ir_addr) >> 2;
		recv->msg.cmd = kreq->ir_command;
		error = kreq->ir_error;
		if (error) {
			TAILQ_REMOVE(&dev->ipmi_completed_requests, kreq,
			    ir_link);
			dev->ipmi_requests--;
			IPMI_UNLOCK(sc);
			ipmi_free_request(kreq);
			return (error);
		}
		len = kreq->ir_replylen + 1;
		if (recv->msg.data_len < len &&
		    (cmd == IPMICTL_RECEIVE_MSG
#ifdef IPMICTL_RECEIVE_MSG_32
		     || cmd == IPMICTL_RECEIVE_MSG_32
#endif
		    )) {
			IPMI_UNLOCK(sc);
			return (EMSGSIZE);
		}
		TAILQ_REMOVE(&dev->ipmi_completed_requests, kreq, ir_link);
		dev->ipmi_requests--;
		IPMI_UNLOCK(sc);
		len = min(recv->msg.data_len, len);
		recv->msg.data_len = len;
		error = copyout(&addr, recv->addr,sizeof(addr));
		if (error == 0)
			error = copyout(&kreq->ir_compcode, recv->msg.data, 1);
		if (error == 0)
			error = copyout(kreq->ir_reply, recv->msg.data + 1,
			    len - 1);
		ipmi_free_request(kreq);
		if (error)
			return (error);
		break;
	case IPMICTL_SET_MY_ADDRESS_CMD:
		IPMI_LOCK(sc);
		dev->ipmi_address = *(int*)data;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_GET_MY_ADDRESS_CMD:
		IPMI_LOCK(sc);
		*(int*)data = dev->ipmi_address;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_SET_MY_LUN_CMD:
		IPMI_LOCK(sc);
		dev->ipmi_lun = *(int*)data & 0x3;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_GET_MY_LUN_CMD:
		IPMI_LOCK(sc);
		*(int*)data = dev->ipmi_lun;
		IPMI_UNLOCK(sc);
		break;
	case IPMICTL_SET_GETS_EVENTS_CMD:
		/*
		device_printf(sc->ipmi_dev,
		    "IPMICTL_SET_GETS_EVENTS_CMD NA\n");
		*/
		break;
	case IPMICTL_REGISTER_FOR_CMD:
	case IPMICTL_UNREGISTER_FOR_CMD:
		return (EOPNOTSUPP);
	default:
		device_printf(sc->ipmi_dev, "Unknown IOCTL %lX\n", cmd);
		return (ENOIOCTL);
	}

#ifdef IPMICTL_SEND_COMMAND_32
	/* Update changed fields in 32-bit structures. */
	switch (cmd) {
	case IPMICTL_RECEIVE_MSG_TRUNC_32:
	case IPMICTL_RECEIVE_MSG_32:
		recv32->recv_type = recv->recv_type;
		recv32->msgid = recv->msgid;
		recv32->msg.netfn = recv->msg.netfn;
		recv32->msg.cmd = recv->msg.cmd;
		recv32->msg.data_len = recv->msg.data_len;
		break;
	}
#endif
	return (0);
}

/*
 * Request management.
 */

static __inline void
ipmi_init_request(struct ipmi_request *req, struct ipmi_device *dev, long msgid,
    uint8_t addr, uint8_t command, size_t requestlen, size_t replylen)
{

	req->ir_owner = dev;
	req->ir_msgid = msgid;
	req->ir_addr = addr;
	req->ir_command = command;
	if (requestlen) {
		req->ir_request = (char *)&req[1];
		req->ir_requestlen = requestlen;
	}
	if (replylen) {
		req->ir_reply = (char *)&req[1] + requestlen;
		req->ir_replybuflen = replylen;
	}
}

/* Allocate a new request with request and reply buffers. */
struct ipmi_request *
ipmi_alloc_request(struct ipmi_device *dev, long msgid, uint8_t addr,
    uint8_t command, size_t requestlen, size_t replylen)
{
	struct ipmi_request *req;

	req = malloc(sizeof(struct ipmi_request) + requestlen + replylen,
	    M_IPMI, M_WAITOK | M_ZERO);
	ipmi_init_request(req, dev, msgid, addr, command, requestlen, replylen);
	return (req);
}

/* Free a request no longer in use. */
void
ipmi_free_request(struct ipmi_request *req)
{

	free(req, M_IPMI);
}

/* Store a processed request on the appropriate completion queue. */
void
ipmi_complete_request(struct ipmi_softc *sc, struct ipmi_request *req)
{
	struct ipmi_device *dev;

	IPMI_LOCK_ASSERT(sc);

	/*
	 * Anonymous requests (from inside the driver) always have a
	 * waiter that we awaken.
	 */
	if (req->ir_owner == NULL)
		wakeup(req);
	else {
		dev = req->ir_owner;
		TAILQ_INSERT_TAIL(&dev->ipmi_completed_requests, req, ir_link);
		selwakeup(&dev->ipmi_select);
		if (dev->ipmi_closing)
			wakeup(&dev->ipmi_requests);
	}
}

/* Perform an internal driver request. */
int
ipmi_submit_driver_request(struct ipmi_softc *sc, struct ipmi_request *req,
    int timo)
{

	return (sc->ipmi_driver_request(sc, req, timo));
}

/*
 * Helper routine for polled system interfaces that use
 * ipmi_polled_enqueue_request() to queue requests.  This request
 * waits until there is a pending request and then returns the first
 * request.  If the driver is shutting down, it returns NULL.
 */
struct ipmi_request *
ipmi_dequeue_request(struct ipmi_softc *sc)
{
	struct ipmi_request *req;

	IPMI_LOCK_ASSERT(sc);

	while (!sc->ipmi_detaching && TAILQ_EMPTY(&sc->ipmi_pending_requests))
		cv_wait(&sc->ipmi_request_added, &sc->ipmi_requests_lock);
	if (sc->ipmi_detaching)
		return (NULL);

	req = TAILQ_FIRST(&sc->ipmi_pending_requests);
	TAILQ_REMOVE(&sc->ipmi_pending_requests, req, ir_link);
	return (req);
}

/* Default implementation of ipmi_enqueue_request() for polled interfaces. */
int
ipmi_polled_enqueue_request(struct ipmi_softc *sc, struct ipmi_request *req)
{

	IPMI_LOCK_ASSERT(sc);

	TAILQ_INSERT_TAIL(&sc->ipmi_pending_requests, req, ir_link);
	cv_signal(&sc->ipmi_request_added);
	return (0);
}

/*
 * Watchdog event handler.
 */

static int
ipmi_reset_watchdog(struct ipmi_softc *sc)
{
	struct ipmi_request *req;
	int error;

	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_RESET_WDOG, 0, 0);
	error = ipmi_submit_driver_request(sc, req, 0);
	if (error)
		device_printf(sc->ipmi_dev, "Failed to reset watchdog\n");
	return (error);
}

static int
ipmi_set_watchdog(struct ipmi_softc *sc, unsigned int sec)
{
	struct ipmi_request *req;
	int error;

	if (sec > 0xffff / 10)
		return (EINVAL);

	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_SET_WDOG, 6, 0);
	if (sec) {
		req->ir_request[0] = IPMI_SET_WD_TIMER_DONT_STOP
		    | IPMI_SET_WD_TIMER_SMS_OS;
		req->ir_request[1] = (wd_timer_actions & 0xff);
		req->ir_request[2] = (wd_pretimeout_countdown & 0xff);
		req->ir_request[3] = 0;	/* Timer use */
		req->ir_request[4] = (sec * 10) & 0xff;
		req->ir_request[5] = (sec * 10) >> 8;
	} else {
		req->ir_request[0] = IPMI_SET_WD_TIMER_SMS_OS;
		req->ir_request[1] = 0;
		req->ir_request[2] = 0;
		req->ir_request[3] = 0;	/* Timer use */
		req->ir_request[4] = 0;
		req->ir_request[5] = 0;
	}
	error = ipmi_submit_driver_request(sc, req, 0);
	if (error)
		device_printf(sc->ipmi_dev, "Failed to set watchdog\n");
	return (error);
}

static void
ipmi_wd_event(void *arg, unsigned int cmd, int *error)
{
	struct ipmi_softc *sc = arg;
	unsigned int timeout;
	int e;

	/* Ignore requests while disabled. */
	if (!on)
		return;

	/*
	 * To prevent infinite hangs, we don't let anyone pat or change
	 * the watchdog when we're shutting down. (See ipmi_shutdown_event().)
	 * However, we do want to keep patting the watchdog while we are doing
	 * a coredump.
	 */
	if (wd_in_shutdown) {
		if (dumping && sc->ipmi_watchdog_active)
			ipmi_reset_watchdog(sc);
		return;
	}

	cmd &= WD_INTERVAL;
	if (cmd > 0 && cmd <= 63) {
		timeout = ((uint64_t)1 << cmd) / 1000000000;
		if (timeout == 0)
			timeout = 1;
		if (timeout != sc->ipmi_watchdog_active ||
		    wd_timer_actions != sc->ipmi_watchdog_actions ||
		    wd_pretimeout_countdown != sc->ipmi_watchdog_pretimeout) {
			e = ipmi_set_watchdog(sc, timeout);
			if (e == 0) {
				sc->ipmi_watchdog_active = timeout;
				sc->ipmi_watchdog_actions = wd_timer_actions;
				sc->ipmi_watchdog_pretimeout = wd_pretimeout_countdown;
			} else {
				(void)ipmi_set_watchdog(sc, 0);
				sc->ipmi_watchdog_active = 0;
				sc->ipmi_watchdog_actions = 0;
				sc->ipmi_watchdog_pretimeout = 0;
			}
		}
		if (sc->ipmi_watchdog_active != 0) {
			e = ipmi_reset_watchdog(sc);
			if (e == 0) {
				*error = 0;
			} else {
				(void)ipmi_set_watchdog(sc, 0);
				sc->ipmi_watchdog_active = 0;
				sc->ipmi_watchdog_actions = 0;
				sc->ipmi_watchdog_pretimeout = 0;
			}
		}
	} else if (atomic_readandclear_int(&sc->ipmi_watchdog_active) != 0) {
		sc->ipmi_watchdog_actions = 0;
		sc->ipmi_watchdog_pretimeout = 0;

		e = ipmi_set_watchdog(sc, 0);
		if (e != 0 && cmd == 0)
			*error = EOPNOTSUPP;
	}
}

static void
ipmi_shutdown_event(void *arg, unsigned int cmd, int *error)
{
	struct ipmi_softc *sc = arg;

	/* Ignore event if disabled. */
	if (!on)
		return;

	/*
	 * Positive wd_shutdown_countdown value will re-arm watchdog;
	 * Zero value in wd_shutdown_countdown will disable watchdog;
	 * Negative value in wd_shutdown_countdown will keep existing state;
	 *
	 * Revert to using a power cycle to ensure that the watchdog will
	 * do something useful here.  Having the watchdog send an NMI
	 * instead is useless during shutdown, and might be ignored if an
	 * NMI already triggered.
	 */

	wd_in_shutdown = true;
	if (wd_shutdown_countdown == 0) {
		/* disable watchdog */
		ipmi_set_watchdog(sc, 0);
		sc->ipmi_watchdog_active = 0;
	} else if (wd_shutdown_countdown > 0) {
		/* set desired action and time, and, reset watchdog */
		wd_timer_actions = IPMI_SET_WD_ACTION_POWER_CYCLE;
		ipmi_set_watchdog(sc, wd_shutdown_countdown);
		sc->ipmi_watchdog_active = wd_shutdown_countdown;
		ipmi_reset_watchdog(sc);
	}
}

static void
ipmi_power_cycle(void *arg, int howto)
{
	struct ipmi_softc *sc = arg;
	struct ipmi_request *req;

	/*
	 * Ignore everything except power cycling requests
	 */
	if ((howto & RB_POWERCYCLE) == 0)
		return;

	device_printf(sc->ipmi_dev, "Power cycling using IPMI\n");

	/*
	 * Send a CHASSIS_CONTROL command to the CHASSIS device, subcommand 2
	 * as described in IPMI v2.0 spec section 28.3.
	 */
	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_CHASSIS_REQUEST, 0),
	    IPMI_CHASSIS_CONTROL, 1, 0);
	req->ir_request[0] = IPMI_CC_POWER_CYCLE;

	ipmi_submit_driver_request(sc, req, MAX_TIMEOUT);

	if (req->ir_error != 0 || req->ir_compcode != 0) {
		device_printf(sc->ipmi_dev, "Power cycling via IPMI failed code %#x %#x\n",
		    req->ir_error, req->ir_compcode);
		return;
	}

	/*
	 * BMCs are notoriously slow, give it cyle_wait seconds for the power
	 * down leg of the power cycle. If that fails, fallback to the next
	 * hanlder in the shutdown_final chain and/or the platform failsafe.
	 */
	DELAY(cycle_wait * 1000 * 1000);
	device_printf(sc->ipmi_dev, "Power cycling via IPMI timed out\n");
}

static void
ipmi_startup(void *arg)
{
	struct ipmi_softc *sc = arg;
	struct ipmi_request *req;
	device_t dev;
	int error, i;

	config_intrhook_disestablish(&sc->ipmi_ich);
	dev = sc->ipmi_dev;

	/* Initialize interface-independent state. */
	mtx_init(&sc->ipmi_requests_lock, "ipmi requests", NULL, MTX_DEF);
	mtx_init(&sc->ipmi_io_lock, "ipmi io", NULL, MTX_DEF);
	cv_init(&sc->ipmi_request_added, "ipmireq");
	TAILQ_INIT(&sc->ipmi_pending_requests);

	/* Initialize interface-dependent state. */
	error = sc->ipmi_startup(sc);
	if (error) {
		device_printf(dev, "Failed to initialize interface: %d\n",
		    error);
		return;
	}

	/* Send a GET_DEVICE_ID request. */
	IPMI_ALLOC_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_GET_DEVICE_ID, 0, 15);

	error = ipmi_submit_driver_request(sc, req, MAX_TIMEOUT);
	if (error == EWOULDBLOCK) {
		device_printf(dev, "Timed out waiting for GET_DEVICE_ID\n");
		return;
	} else if (error) {
		device_printf(dev, "Failed GET_DEVICE_ID: %d\n", error);
		return;
	} else if (req->ir_compcode != 0) {
		device_printf(dev,
		    "Bad completion code for GET_DEVICE_ID: %d\n",
		    req->ir_compcode);
		return;
	} else if (req->ir_replylen < 5) {
		device_printf(dev, "Short reply for GET_DEVICE_ID: %d\n",
		    req->ir_replylen);
		return;
	}

	device_printf(dev, "IPMI device rev. %d, firmware rev. %d.%d%d, "
	    "version %d.%d, device support mask %#x\n",
	    req->ir_reply[1] & 0x0f,
	    req->ir_reply[2] & 0x7f, req->ir_reply[3] >> 4, req->ir_reply[3] & 0x0f,
	    req->ir_reply[4] & 0x0f, req->ir_reply[4] >> 4, req->ir_reply[5]);

	sc->ipmi_dev_support = req->ir_reply[5];

	IPMI_INIT_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
	    IPMI_CLEAR_FLAGS, 1, 0);

	ipmi_submit_driver_request(sc, req, 0);

	/* XXX: Magic numbers */
	if (req->ir_compcode == 0xc0) {
		device_printf(dev, "Clear flags is busy\n");
	}
	if (req->ir_compcode == 0xc1) {
		device_printf(dev, "Clear flags illegal\n");
	}

	for (i = 0; i < 8; i++) {
		IPMI_INIT_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
		    IPMI_GET_CHANNEL_INFO, 1, 0);
		req->ir_request[0] = i;

		ipmi_submit_driver_request(sc, req, 0);

		if (req->ir_compcode != 0)
			break;
	}
	device_printf(dev, "Number of channels %d\n", i);

	/*
	 * Probe for watchdog, but only for backends which support
	 * polled driver requests.
	 */
	if (sc->ipmi_driver_requests_polled) {
		IPMI_INIT_DRIVER_REQUEST(req, IPMI_ADDR(IPMI_APP_REQUEST, 0),
		    IPMI_GET_WDOG, 0, 0);

		ipmi_submit_driver_request(sc, req, 0);

		if (req->ir_compcode == 0x00) {
			device_printf(dev, "Attached watchdog\n");
			/* register the watchdog event handler */
			sc->ipmi_watchdog_tag = EVENTHANDLER_REGISTER(
				watchdog_list, ipmi_wd_event, sc, 0);
			sc->ipmi_shutdown_tag = EVENTHANDLER_REGISTER(
				shutdown_pre_sync, ipmi_shutdown_event,
				sc, 0);
		}
	}

	sc->ipmi_cdev = make_dev(&ipmi_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_OPERATOR, 0660, "ipmi%d", device_get_unit(dev));
	if (sc->ipmi_cdev == NULL) {
		device_printf(dev, "Failed to create cdev\n");
		return;
	}
	sc->ipmi_cdev->si_drv1 = sc;

	/*
	 * Set initial watchdog state. If desired, set an initial
	 * watchdog on startup. Or, if the watchdog device is
	 * disabled, clear any existing watchdog.
	 */
	if (on && wd_startup_countdown > 0) {
		wd_timer_actions = IPMI_SET_WD_ACTION_POWER_CYCLE;
		if (ipmi_set_watchdog(sc, wd_startup_countdown) == 0 &&
		    ipmi_reset_watchdog(sc) == 0) {
			sc->ipmi_watchdog_active = wd_startup_countdown;
			sc->ipmi_watchdog_actions = wd_timer_actions;
			sc->ipmi_watchdog_pretimeout = wd_pretimeout_countdown;
		} else
			(void)ipmi_set_watchdog(sc, 0);
		ipmi_reset_watchdog(sc);
	} else if (!on)
		(void)ipmi_set_watchdog(sc, 0);
	/*
	 * Power cycle the system off using IPMI. We use last - 2 since we don't
	 * handle all the other kinds of reboots. We'll let others handle them.
	 * We only try to do this if the BMC supports the Chassis device.
	 */
	if (sc->ipmi_dev_support & IPMI_ADS_CHASSIS) {
		device_printf(dev, "Establishing power cycle handler\n");
		sc->ipmi_power_cycle_tag = EVENTHANDLER_REGISTER(shutdown_final,
		    ipmi_power_cycle, sc, SHUTDOWN_PRI_LAST - 2);
	}
}

int
ipmi_attach(device_t dev)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	int error;

	if (sc->ipmi_irq_res != NULL && sc->ipmi_intr != NULL) {
		error = bus_setup_intr(dev, sc->ipmi_irq_res, INTR_TYPE_MISC,
		    NULL, sc->ipmi_intr, sc, &sc->ipmi_irq);
		if (error) {
			device_printf(dev, "can't set up interrupt\n");
			return (error);
		}
	}

	bzero(&sc->ipmi_ich, sizeof(struct intr_config_hook));
	sc->ipmi_ich.ich_func = ipmi_startup;
	sc->ipmi_ich.ich_arg = sc;
	if (config_intrhook_establish(&sc->ipmi_ich) != 0) {
		device_printf(dev, "can't establish configuration hook\n");
		return (ENOMEM);
	}

	ipmi_attached = 1;
	return (0);
}

int
ipmi_detach(device_t dev)
{
	struct ipmi_softc *sc;

	sc = device_get_softc(dev);

	/* Fail if there are any open handles. */
	IPMI_LOCK(sc);
	if (sc->ipmi_opened) {
		IPMI_UNLOCK(sc);
		return (EBUSY);
	}
	IPMI_UNLOCK(sc);
	if (sc->ipmi_cdev)
		destroy_dev(sc->ipmi_cdev);

	/* Detach from watchdog handling and turn off watchdog. */
	if (sc->ipmi_shutdown_tag)
		EVENTHANDLER_DEREGISTER(shutdown_pre_sync,
		sc->ipmi_shutdown_tag);
	if (sc->ipmi_watchdog_tag) {
		EVENTHANDLER_DEREGISTER(watchdog_list, sc->ipmi_watchdog_tag);
		ipmi_set_watchdog(sc, 0);
	}

	/* Detach from shutdown handling for power cycle reboot */
	if (sc->ipmi_power_cycle_tag)
		EVENTHANDLER_DEREGISTER(shutdown_final, sc->ipmi_power_cycle_tag);

	/* XXX: should use shutdown callout I think. */
	/* If the backend uses a kthread, shut it down. */
	IPMI_LOCK(sc);
	sc->ipmi_detaching = 1;
	if (sc->ipmi_kthread) {
		cv_broadcast(&sc->ipmi_request_added);
		msleep(sc->ipmi_kthread, &sc->ipmi_requests_lock, 0,
		    "ipmi_wait", 0);
	}
	IPMI_UNLOCK(sc);
	if (sc->ipmi_irq)
		bus_teardown_intr(dev, sc->ipmi_irq_res, sc->ipmi_irq);

	ipmi_release_resources(dev);
	mtx_destroy(&sc->ipmi_io_lock);
	mtx_destroy(&sc->ipmi_requests_lock);
	return (0);
}

void
ipmi_release_resources(device_t dev)
{
	struct ipmi_softc *sc;
	int i;

	sc = device_get_softc(dev);
	if (sc->ipmi_irq)
		bus_teardown_intr(dev, sc->ipmi_irq_res, sc->ipmi_irq);
	if (sc->ipmi_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->ipmi_irq_rid,
		    sc->ipmi_irq_res);
	for (i = 0; i < MAX_RES; i++)
		if (sc->ipmi_io_res[i])
			bus_release_resource(dev, sc->ipmi_io_type,
			    sc->ipmi_io_rid + i, sc->ipmi_io_res[i]);
}

devclass_t ipmi_devclass;

/* XXX: Why? */
static void
ipmi_unload(void *arg)
{
	device_t *	devs;
	int		count;
	int		i;

	if (ipmi_devclass == NULL)
		return;
	if (devclass_get_devices(ipmi_devclass, &devs, &count) != 0)
		return;
	for (i = 0; i < count; i++)
		device_delete_child(device_get_parent(devs[i]), devs[i]);
	free(devs, M_TEMP);
}
SYSUNINIT(ipmi_unload, SI_SUB_DRIVERS, SI_ORDER_FIRST, ipmi_unload, NULL);

#ifdef IMPI_DEBUG
static void
dump_buf(u_char *data, int len)
{
	char buf[20];
	char line[1024];
	char temp[30];
	int count = 0;
	int i=0;

	printf("Address %p len %d\n", data, len);
	if (len > 256)
		len = 256;
	line[0] = '\000';
	for (; len > 0; len--, data++) {
		sprintf(temp, "%02x ", *data);
		strcat(line, temp);
		if (*data >= ' ' && *data <= '~')
			buf[count] = *data;
		else if (*data >= 'A' && *data <= 'Z')
			buf[count] = *data;
		else
			buf[count] = '.';
		if (++count == 16) {
			buf[count] = '\000';
			count = 0;
			printf("  %3x  %s %s\n", i, line, buf);
			i+=16;
			line[0] = '\000';
		}
	}
	buf[count] = '\000';

	for (; count != 16; count++) {
		strcat(line, "   ");
	}
	printf("  %3x  %s %s\n", i, line, buf);
}
#endif
