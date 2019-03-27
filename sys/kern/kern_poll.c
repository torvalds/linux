/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Luigi Rizzo
 *
 * Supported by: the Xorp Project (www.xorp.org)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include "opt_device_polling.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/eventhandler.h>
#include <sys/resourcevar.h>
#include <sys/socket.h>			/* needed by net/if.h		*/
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/netisr.h>			/* for NETISR_POLL		*/
#include <net/vnet.h>

void hardclock_device_poll(void);	/* hook from hardclock		*/

static struct mtx	poll_mtx;

/*
 * Polling support for [network] device drivers.
 *
 * Drivers which support this feature can register with the
 * polling code.
 *
 * If registration is successful, the driver must disable interrupts,
 * and further I/O is performed through the handler, which is invoked
 * (at least once per clock tick) with 3 arguments: the "arg" passed at
 * register time (a struct ifnet pointer), a command, and a "count" limit.
 *
 * The command can be one of the following:
 *  POLL_ONLY: quick move of "count" packets from input/output queues.
 *  POLL_AND_CHECK_STATUS: as above, plus check status registers or do
 *	other more expensive operations. This command is issued periodically
 *	but less frequently than POLL_ONLY.
 *
 * The count limit specifies how much work the handler can do during the
 * call -- typically this is the number of packets to be received, or
 * transmitted, etc. (drivers are free to interpret this number, as long
 * as the max time spent in the function grows roughly linearly with the
 * count).
 *
 * Polling is enabled and disabled via setting IFCAP_POLLING flag on
 * the interface. The driver ioctl handler should register interface
 * with polling and disable interrupts, if registration was successful.
 *
 * A second variable controls the sharing of CPU between polling/kernel
 * network processing, and other activities (typically userlevel tasks):
 * kern.polling.user_frac (between 0 and 100, default 50) sets the share
 * of CPU allocated to user tasks. CPU is allocated proportionally to the
 * shares, by dynamically adjusting the "count" (poll_burst).
 *
 * Other parameters can should be left to their default values.
 * The following constraints hold
 *
 *	1 <= poll_each_burst <= poll_burst <= poll_burst_max
 *	MIN_POLL_BURST_MAX <= poll_burst_max <= MAX_POLL_BURST_MAX
 */

#define MIN_POLL_BURST_MAX	10
#define MAX_POLL_BURST_MAX	20000

static uint32_t poll_burst = 5;
static uint32_t poll_burst_max = 150;	/* good for 100Mbit net and HZ=1000 */
static uint32_t poll_each_burst = 5;

static SYSCTL_NODE(_kern, OID_AUTO, polling, CTLFLAG_RW, 0,
	"Device polling parameters");

SYSCTL_UINT(_kern_polling, OID_AUTO, burst, CTLFLAG_RD,
	&poll_burst, 0, "Current polling burst size");

static int	netisr_poll_scheduled;
static int	netisr_pollmore_scheduled;
static int	poll_shutting_down;

static int poll_burst_max_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = poll_burst_max;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < MIN_POLL_BURST_MAX || val > MAX_POLL_BURST_MAX)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	poll_burst_max = val;
	if (poll_burst > poll_burst_max)
		poll_burst = poll_burst_max;
	if (poll_each_burst > poll_burst_max)
		poll_each_burst = MIN_POLL_BURST_MAX;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, burst_max, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), poll_burst_max_sysctl, "I", "Max Polling burst size");

static int poll_each_burst_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = poll_each_burst;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < 1)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	if (val > poll_burst_max) {
		mtx_unlock(&poll_mtx);
		return (EINVAL);
	}
	poll_each_burst = val;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, each_burst, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), poll_each_burst_sysctl, "I",
	"Max size of each burst");

static uint32_t poll_in_idle_loop=0;	/* do we poll in idle loop ? */
SYSCTL_UINT(_kern_polling, OID_AUTO, idle_poll, CTLFLAG_RW,
	&poll_in_idle_loop, 0, "Enable device polling in idle loop");

static uint32_t user_frac = 50;
static int user_frac_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = user_frac;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val > 99)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	user_frac = val;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, user_frac, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), user_frac_sysctl, "I",
	"Desired user fraction of cpu time");

static uint32_t reg_frac_count = 0;
static uint32_t reg_frac = 20 ;
static int reg_frac_sysctl(SYSCTL_HANDLER_ARGS)
{
	uint32_t val = reg_frac;
	int error;

	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || !req->newptr )
		return (error);
	if (val < 1 || val > hz)
		return (EINVAL);

	mtx_lock(&poll_mtx);
	reg_frac = val;
	if (reg_frac_count >= reg_frac)
		reg_frac_count = 0;
	mtx_unlock(&poll_mtx);

	return (0);
}
SYSCTL_PROC(_kern_polling, OID_AUTO, reg_frac, CTLTYPE_UINT | CTLFLAG_RW,
	0, sizeof(uint32_t), reg_frac_sysctl, "I",
	"Every this many cycles check registers");

static uint32_t short_ticks;
SYSCTL_UINT(_kern_polling, OID_AUTO, short_ticks, CTLFLAG_RD,
	&short_ticks, 0, "Hardclock ticks shorter than they should be");

static uint32_t lost_polls;
SYSCTL_UINT(_kern_polling, OID_AUTO, lost_polls, CTLFLAG_RD,
	&lost_polls, 0, "How many times we would have lost a poll tick");

static uint32_t pending_polls;
SYSCTL_UINT(_kern_polling, OID_AUTO, pending_polls, CTLFLAG_RD,
	&pending_polls, 0, "Do we need to poll again");

static int residual_burst = 0;
SYSCTL_INT(_kern_polling, OID_AUTO, residual_burst, CTLFLAG_RD,
	&residual_burst, 0, "# of residual cycles in burst");

static uint32_t poll_handlers; /* next free entry in pr[]. */
SYSCTL_UINT(_kern_polling, OID_AUTO, handlers, CTLFLAG_RD,
	&poll_handlers, 0, "Number of registered poll handlers");

static uint32_t phase;
SYSCTL_UINT(_kern_polling, OID_AUTO, phase, CTLFLAG_RD,
	&phase, 0, "Polling phase");

static uint32_t suspect;
SYSCTL_UINT(_kern_polling, OID_AUTO, suspect, CTLFLAG_RD,
	&suspect, 0, "suspect event");

static uint32_t stalled;
SYSCTL_UINT(_kern_polling, OID_AUTO, stalled, CTLFLAG_RD,
	&stalled, 0, "potential stalls");

static uint32_t idlepoll_sleeping; /* idlepoll is sleeping */
SYSCTL_UINT(_kern_polling, OID_AUTO, idlepoll_sleeping, CTLFLAG_RD,
	&idlepoll_sleeping, 0, "idlepoll is sleeping");


#define POLL_LIST_LEN  128
struct pollrec {
	poll_handler_t	*handler;
	struct ifnet	*ifp;
};

static struct pollrec pr[POLL_LIST_LEN];

static void
poll_shutdown(void *arg, int howto)
{

	poll_shutting_down = 1;
}

static void
init_device_poll(void)
{

	mtx_init(&poll_mtx, "polling", NULL, MTX_DEF);
	EVENTHANDLER_REGISTER(shutdown_post_sync, poll_shutdown, NULL,
	    SHUTDOWN_PRI_LAST);
}
SYSINIT(device_poll, SI_SUB_SOFTINTR, SI_ORDER_MIDDLE, init_device_poll, NULL);


/*
 * Hook from hardclock. Tries to schedule a netisr, but keeps track
 * of lost ticks due to the previous handler taking too long.
 * Normally, this should not happen, because polling handler should
 * run for a short time. However, in some cases (e.g. when there are
 * changes in link status etc.) the drivers take a very long time
 * (even in the order of milliseconds) to reset and reconfigure the
 * device, causing apparent lost polls.
 *
 * The first part of the code is just for debugging purposes, and tries
 * to count how often hardclock ticks are shorter than they should,
 * meaning either stray interrupts or delayed events.
 */
void
hardclock_device_poll(void)
{
	static struct timeval prev_t, t;
	int delta;

	if (poll_handlers == 0 || poll_shutting_down)
		return;

	microuptime(&t);
	delta = (t.tv_usec - prev_t.tv_usec) +
		(t.tv_sec - prev_t.tv_sec)*1000000;
	if (delta * hz < 500000)
		short_ticks++;
	else
		prev_t = t;

	if (pending_polls > 100) {
		/*
		 * Too much, assume it has stalled (not always true
		 * see comment above).
		 */
		stalled++;
		pending_polls = 0;
		phase = 0;
	}

	if (phase <= 2) {
		if (phase != 0)
			suspect++;
		phase = 1;
		netisr_poll_scheduled = 1;
		netisr_pollmore_scheduled = 1;
		netisr_sched_poll();
		phase = 2;
	}
	if (pending_polls++ > 0)
		lost_polls++;
}

/*
 * ether_poll is called from the idle loop.
 */
static void
ether_poll(int count)
{
	int i;

	mtx_lock(&poll_mtx);

	if (count > poll_each_burst)
		count = poll_each_burst;

	for (i = 0 ; i < poll_handlers ; i++)
		pr[i].handler(pr[i].ifp, POLL_ONLY, count);

	mtx_unlock(&poll_mtx);
}

/*
 * netisr_pollmore is called after other netisr's, possibly scheduling
 * another NETISR_POLL call, or adapting the burst size for the next cycle.
 *
 * It is very bad to fetch large bursts of packets from a single card at once,
 * because the burst could take a long time to be completely processed, or
 * could saturate the intermediate queue (ipintrq or similar) leading to
 * losses or unfairness. To reduce the problem, and also to account better for
 * time spent in network-related processing, we split the burst in smaller
 * chunks of fixed size, giving control to the other netisr's between chunks.
 * This helps in improving the fairness, reducing livelock (because we
 * emulate more closely the "process to completion" that we have with
 * fastforwarding) and accounting for the work performed in low level
 * handling and forwarding.
 */

static struct timeval poll_start_t;

void
netisr_pollmore()
{
	struct timeval t;
	int kern_load;

	if (poll_handlers == 0)
		return;

	mtx_lock(&poll_mtx);
	if (!netisr_pollmore_scheduled) {
		mtx_unlock(&poll_mtx);
		return;
	}
	netisr_pollmore_scheduled = 0;
	phase = 5;
	if (residual_burst > 0) {
		netisr_poll_scheduled = 1;
		netisr_pollmore_scheduled = 1;
		netisr_sched_poll();
		mtx_unlock(&poll_mtx);
		/* will run immediately on return, followed by netisrs */
		return;
	}
	/* here we can account time spent in netisr's in this tick */
	microuptime(&t);
	kern_load = (t.tv_usec - poll_start_t.tv_usec) +
		(t.tv_sec - poll_start_t.tv_sec)*1000000;	/* us */
	kern_load = (kern_load * hz) / 10000;			/* 0..100 */
	if (kern_load > (100 - user_frac)) { /* try decrease ticks */
		if (poll_burst > 1)
			poll_burst--;
	} else {
		if (poll_burst < poll_burst_max)
			poll_burst++;
	}

	pending_polls--;
	if (pending_polls == 0) /* we are done */
		phase = 0;
	else {
		/*
		 * Last cycle was long and caused us to miss one or more
		 * hardclock ticks. Restart processing again, but slightly
		 * reduce the burst size to prevent that this happens again.
		 */
		poll_burst -= (poll_burst / 8);
		if (poll_burst < 1)
			poll_burst = 1;
		netisr_poll_scheduled = 1;
		netisr_pollmore_scheduled = 1;
		netisr_sched_poll();
		phase = 6;
	}
	mtx_unlock(&poll_mtx);
}

/*
 * netisr_poll is typically scheduled once per tick.
 */
void
netisr_poll(void)
{
	int i, cycles;
	enum poll_cmd arg = POLL_ONLY;

	if (poll_handlers == 0)
		return;

	mtx_lock(&poll_mtx);
	if (!netisr_poll_scheduled) {
		mtx_unlock(&poll_mtx);
		return;
	}
	netisr_poll_scheduled = 0;
	phase = 3;
	if (residual_burst == 0) { /* first call in this tick */
		microuptime(&poll_start_t);
		if (++reg_frac_count == reg_frac) {
			arg = POLL_AND_CHECK_STATUS;
			reg_frac_count = 0;
		}

		residual_burst = poll_burst;
	}
	cycles = (residual_burst < poll_each_burst) ?
		residual_burst : poll_each_burst;
	residual_burst -= cycles;

	for (i = 0 ; i < poll_handlers ; i++)
		pr[i].handler(pr[i].ifp, arg, cycles);

	phase = 4;
	mtx_unlock(&poll_mtx);
}

/*
 * Try to register routine for polling. Returns 0 if successful
 * (and polling should be enabled), error code otherwise.
 * A device is not supposed to register itself multiple times.
 *
 * This is called from within the *_ioctl() functions.
 */
int
ether_poll_register(poll_handler_t *h, if_t ifp)
{
	int i;

	KASSERT(h != NULL, ("%s: handler is NULL", __func__));
	KASSERT(ifp != NULL, ("%s: ifp is NULL", __func__));

	mtx_lock(&poll_mtx);
	if (poll_handlers >= POLL_LIST_LEN) {
		/*
		 * List full, cannot register more entries.
		 * This should never happen; if it does, it is probably a
		 * broken driver trying to register multiple times. Checking
		 * this at runtime is expensive, and won't solve the problem
		 * anyways, so just report a few times and then give up.
		 */
		static int verbose = 10 ;
		if (verbose >0) {
			log(LOG_ERR, "poll handlers list full, "
			    "maybe a broken driver ?\n");
			verbose--;
		}
		mtx_unlock(&poll_mtx);
		return (ENOMEM); /* no polling for you */
	}

	for (i = 0 ; i < poll_handlers ; i++)
		if (pr[i].ifp == ifp && pr[i].handler != NULL) {
			mtx_unlock(&poll_mtx);
			log(LOG_DEBUG, "ether_poll_register: %s: handler"
			    " already registered\n", ifp->if_xname);
			return (EEXIST);
		}

	pr[poll_handlers].handler = h;
	pr[poll_handlers].ifp = ifp;
	poll_handlers++;
	mtx_unlock(&poll_mtx);
	if (idlepoll_sleeping)
		wakeup(&idlepoll_sleeping);
	return (0);
}

/*
 * Remove interface from the polling list. Called from *_ioctl(), too.
 */
int
ether_poll_deregister(if_t ifp)
{
	int i;

	KASSERT(ifp != NULL, ("%s: ifp is NULL", __func__));

	mtx_lock(&poll_mtx);

	for (i = 0 ; i < poll_handlers ; i++)
		if (pr[i].ifp == ifp) /* found it */
			break;
	if (i == poll_handlers) {
		log(LOG_DEBUG, "ether_poll_deregister: %s: not found!\n",
		    ifp->if_xname);
		mtx_unlock(&poll_mtx);
		return (ENOENT);
	}
	poll_handlers--;
	if (i < poll_handlers) { /* Last entry replaces this one. */
		pr[i].handler = pr[poll_handlers].handler;
		pr[i].ifp = pr[poll_handlers].ifp;
	}
	mtx_unlock(&poll_mtx);
	return (0);
}

static void
poll_idle(void)
{
	struct thread *td = curthread;
	struct rtprio rtp;

	rtp.prio = RTP_PRIO_MAX;	/* lowest priority */
	rtp.type = RTP_PRIO_IDLE;
	PROC_SLOCK(td->td_proc);
	rtp_to_pri(&rtp, td);
	PROC_SUNLOCK(td->td_proc);

	for (;;) {
		if (poll_in_idle_loop && poll_handlers > 0) {
			idlepoll_sleeping = 0;
			ether_poll(poll_each_burst);
			thread_lock(td);
			mi_switch(SW_VOL, NULL);
			thread_unlock(td);
		} else {
			idlepoll_sleeping = 1;
			tsleep(&idlepoll_sleeping, 0, "pollid", hz * 3);
		}
	}
}

static struct proc *idlepoll;
static struct kproc_desc idlepoll_kp = {
	 "idlepoll",
	 poll_idle,
	 &idlepoll
};
SYSINIT(idlepoll, SI_SUB_KTHREAD_VM, SI_ORDER_ANY, kproc_start,
    &idlepoll_kp);
