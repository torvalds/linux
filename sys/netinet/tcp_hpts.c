/*-
 * Copyright (c) 2016-2018 Netflix, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_tcpdebug.h"
/**
 * Some notes about usage.
 *
 * The tcp_hpts system is designed to provide a high precision timer
 * system for tcp. Its main purpose is to provide a mechanism for 
 * pacing packets out onto the wire. It can be used in two ways
 * by a given TCP stack (and those two methods can be used simultaneously).
 *
 * First, and probably the main thing its used by Rack and BBR for, it can
 * be used to call tcp_output() of a transport stack at some time in the future.
 * The normal way this is done is that tcp_output() of the stack schedules
 * itself to be called again by calling tcp_hpts_insert(tcpcb, slot). The
 * slot is the time from now that the stack wants to be called but it
 * must be converted to tcp_hpts's notion of slot. This is done with
 * one of the macros HPTS_MS_TO_SLOTS or HPTS_USEC_TO_SLOTS. So a typical
 * call from the tcp_output() routine might look like:
 *
 * tcp_hpts_insert(tp, HPTS_USEC_TO_SLOTS(550));
 *
 * The above would schedule tcp_ouput() to be called in 550 useconds.
 * Note that if using this mechanism the stack will want to add near
 * its top a check to prevent unwanted calls (from user land or the
 * arrival of incoming ack's). So it would add something like:
 *
 * if (inp->inp_in_hpts)
 *    return;
 *
 * to prevent output processing until the time alotted has gone by.
 * Of course this is a bare bones example and the stack will probably
 * have more consideration then just the above.
 *
 * Now the tcp_hpts system will call tcp_output in one of two forms, 
 * it will first check to see if the stack as defined a 
 * tfb_tcp_output_wtime() function, if so that is the routine it
 * will call, if that function is not defined then it will call the
 * tfb_tcp_output() function. The only difference between these
 * two calls is that the former passes the time in to the function
 * so the function does not have to access the time (which tcp_hpts
 * already has). What these functions do is of course totally up
 * to the individual tcp stack.
 *
 * Now the second function (actually two functions I guess :D)
 * the tcp_hpts system provides is the  ability to either abort 
 * a connection (later) or process  input on a connection. 
 * Why would you want to do this? To keep processor locality.
 *
 * So in order to use the input redirection function the
 * stack changes its tcp_do_segment() routine to instead
 * of process the data call the function:
 *
 * tcp_queue_pkt_to_input()
 *
 * You will note that the arguments to this function look
 * a lot like tcp_do_segments's arguments. This function
 * will assure that the tcp_hpts system will
 * call the functions tfb_tcp_hpts_do_segment() from the
 * correct CPU. Note that multiple calls can get pushed
 * into the tcp_hpts system this will be indicated by
 * the next to last argument to tfb_tcp_hpts_do_segment()
 * (nxt_pkt). If nxt_pkt is a 1 then another packet is
 * coming. If nxt_pkt is a 0 then this is the last call
 * that the tcp_hpts system has available for the tcp stack.
 * 
 * The other point of the input system is to be able to safely
 * drop a tcp connection without worrying about the recursive 
 * locking that may be occuring on the INP_WLOCK. So if
 * a stack wants to drop a connection it calls:
 *
 *     tcp_set_inp_to_drop(tp, ETIMEDOUT)
 * 
 * To schedule the tcp_hpts system to call 
 * 
 *    tcp_drop(tp, drop_reason)
 *
 * at a future point. This is quite handy to prevent locking
 * issues when dropping connections.
 *
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/hhook.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>		/* for proc0 declaration */
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/refcount.h>
#include <sys/sched.h>
#include <sys/queue.h>
#include <sys/smp.h>
#include <sys/counter.h>
#include <sys/time.h>
#include <sys/kthread.h>
#include <sys/kern_prefetch.h>

#include <vm/uma.h>

#include <net/route.h>
#include <net/vnet.h>

#define TCPSTATES		/* for logging */

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>	/* required for icmp_var.h */
#include <netinet/icmp_var.h>	/* for ICMP_BANDLIM */
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/cc/cc.h>
#include <netinet/tcp_hpts.h>

#ifdef tcpdebug
#include <netinet/tcp_debug.h>
#endif				/* tcpdebug */
#ifdef tcp_offload
#include <netinet/tcp_offload.h>
#endif

#include "opt_rss.h"

MALLOC_DEFINE(M_TCPHPTS, "tcp_hpts", "TCP hpts");
#ifdef RSS
#include <net/netisr.h>
#include <net/rss_config.h>
static int tcp_bind_threads = 1;
#else
static int tcp_bind_threads = 0;
#endif
TUNABLE_INT("net.inet.tcp.bind_hptss", &tcp_bind_threads);

static uint32_t tcp_hpts_logging_size = DEFAULT_HPTS_LOG;

TUNABLE_INT("net.inet.tcp.hpts_logging_sz", &tcp_hpts_logging_size);

static struct tcp_hptsi tcp_pace;

static void tcp_wakehpts(struct tcp_hpts_entry *p);
static void tcp_wakeinput(struct tcp_hpts_entry *p);
static void tcp_input_data(struct tcp_hpts_entry *hpts, struct timeval *tv);
static void tcp_hptsi(struct tcp_hpts_entry *hpts, struct timeval *ctick);
static void tcp_hpts_thread(void *ctx);
static void tcp_init_hptsi(void *st);

int32_t tcp_min_hptsi_time = DEFAULT_MIN_SLEEP;
static int32_t tcp_hpts_callout_skip_swi = 0;

SYSCTL_NODE(_net_inet_tcp, OID_AUTO, hpts, CTLFLAG_RW, 0, "TCP Hpts controls");

#define	timersub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;	\
		if ((vvp)->tv_usec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_usec += 1000000;			\
		}							\
	} while (0)

static int32_t logging_on = 0;
static int32_t hpts_sleep_max = (NUM_OF_HPTSI_SLOTS - 2);
static int32_t tcp_hpts_precision = 120;

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, precision, CTLFLAG_RW,
    &tcp_hpts_precision, 120,
    "Value for PRE() precision of callout");

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, logging, CTLFLAG_RW,
    &logging_on, 0,
    "Turn on logging if compiled in");

counter_u64_t hpts_loops;

SYSCTL_COUNTER_U64(_net_inet_tcp_hpts, OID_AUTO, loops, CTLFLAG_RD,
    &hpts_loops, "Number of times hpts had to loop to catch up");

counter_u64_t back_tosleep;

SYSCTL_COUNTER_U64(_net_inet_tcp_hpts, OID_AUTO, no_tcbsfound, CTLFLAG_RD,
    &back_tosleep, "Number of times hpts found no tcbs");

static int32_t in_newts_every_tcb = 0;

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, in_tsperpcb, CTLFLAG_RW,
    &in_newts_every_tcb, 0,
    "Do we have a new cts every tcb we process for input");
static int32_t in_ts_percision = 0;

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, in_tspercision, CTLFLAG_RW,
    &in_ts_percision, 0,
    "Do we use percise timestamp for clients on input");
static int32_t out_newts_every_tcb = 0;

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, out_tsperpcb, CTLFLAG_RW,
    &out_newts_every_tcb, 0,
    "Do we have a new cts every tcb we process for output");
static int32_t out_ts_percision = 0;

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, out_tspercision, CTLFLAG_RW,
    &out_ts_percision, 0,
    "Do we use a percise timestamp for every output cts");

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, maxsleep, CTLFLAG_RW,
    &hpts_sleep_max, 0,
    "The maximum time the hpts will sleep <1 - 254>");

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, minsleep, CTLFLAG_RW,
    &tcp_min_hptsi_time, 0,
    "The minimum time the hpts must sleep before processing more slots");

SYSCTL_INT(_net_inet_tcp_hpts, OID_AUTO, skip_swi, CTLFLAG_RW,
    &tcp_hpts_callout_skip_swi, 0,
    "Do we have the callout call directly to the hpts?");

static void
__tcp_hpts_log_it(struct tcp_hpts_entry *hpts, struct inpcb *inp, int event, uint32_t slot,
    uint32_t ticknow, int32_t line)
{
	struct hpts_log *pl;

	HPTS_MTX_ASSERT(hpts);
	if (hpts->p_log == NULL)
		return;
	pl = &hpts->p_log[hpts->p_log_at];
	hpts->p_log_at++;
	if (hpts->p_log_at >= hpts->p_logsize) {
		hpts->p_log_at = 0;
		hpts->p_log_wrapped = 1;
	}
	pl->inp = inp;
	if (inp) {
		pl->t_paceslot = inp->inp_hptsslot;
		pl->t_hptsreq = inp->inp_hpts_request;
		pl->p_onhpts = inp->inp_in_hpts;
		pl->p_oninput = inp->inp_in_input;
	} else {
		pl->t_paceslot = 0;
		pl->t_hptsreq = 0;
		pl->p_onhpts = 0;
		pl->p_oninput = 0;
	}
	pl->is_notempty = 1;
	pl->event = event;
	pl->line = line;
	pl->cts = tcp_get_usecs(NULL);
	pl->p_curtick = hpts->p_curtick;
	pl->p_prevtick = hpts->p_prevtick;
	pl->p_on_queue_cnt = hpts->p_on_queue_cnt;
	pl->ticknow = ticknow;
	pl->slot_req = slot;
	pl->p_nxt_slot = hpts->p_nxt_slot;
	pl->p_cur_slot = hpts->p_cur_slot;
	pl->p_hpts_sleep_time = hpts->p_hpts_sleep_time;
	pl->p_flags = (hpts->p_cpu & 0x7f);
	pl->p_flags <<= 7;
	pl->p_flags |= (hpts->p_num & 0x7f);
	pl->p_flags <<= 2;
	if (hpts->p_hpts_active) {
		pl->p_flags |= HPTS_HPTS_ACTIVE;
	}
}

#define tcp_hpts_log_it(a, b, c, d, e) __tcp_hpts_log_it(a, b, c, d, e, __LINE__)

static void
hpts_timeout_swi(void *arg)
{
	struct tcp_hpts_entry *hpts;

	hpts = (struct tcp_hpts_entry *)arg;
	swi_sched(hpts->ie_cookie, 0);
}

static void
hpts_timeout_dir(void *arg)
{
	tcp_hpts_thread(arg);
}

static inline void
hpts_sane_pace_remove(struct tcp_hpts_entry *hpts, struct inpcb *inp, struct hptsh *head, int clear)
{
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx) == 0) {
		/* We don't own the mutex? */
		panic("%s: hpts:%p inp:%p no hpts mutex", __FUNCTION__, hpts, inp);
	}
	if (hpts->p_cpu != inp->inp_hpts_cpu) {
		/* It is not the right cpu/mutex? */
		panic("%s: hpts:%p inp:%p incorrect CPU", __FUNCTION__, hpts, inp);
	}
	if (inp->inp_in_hpts == 0) {
		/* We are not on the hpts? */
		panic("%s: hpts:%p inp:%p not on the hpts?", __FUNCTION__, hpts, inp);
	}
	if (TAILQ_EMPTY(head) &&
	    (hpts->p_on_queue_cnt != 0)) {
		/* We should not be empty with a queue count */
		panic("%s hpts:%p hpts bucket empty but cnt:%d",
		    __FUNCTION__, hpts, hpts->p_on_queue_cnt);
	}
#endif
	TAILQ_REMOVE(head, inp, inp_hpts);
	hpts->p_on_queue_cnt--;
	if (hpts->p_on_queue_cnt < 0) {
		/* Count should not go negative .. */
#ifdef INVARIANTS
		panic("Hpts goes negative inp:%p hpts:%p",
		    inp, hpts);
#endif
		hpts->p_on_queue_cnt = 0;
	}
	if (clear) {
		inp->inp_hpts_request = 0;
		inp->inp_in_hpts = 0;
	}
}

static inline void
hpts_sane_pace_insert(struct tcp_hpts_entry *hpts, struct inpcb *inp, struct hptsh *head, int line, int noref)
{
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx) == 0) {
		/* We don't own the mutex? */
		panic("%s: hpts:%p inp:%p no hpts mutex", __FUNCTION__, hpts, inp);
	}
	if (hpts->p_cpu != inp->inp_hpts_cpu) {
		/* It is not the right cpu/mutex? */
		panic("%s: hpts:%p inp:%p incorrect CPU", __FUNCTION__, hpts, inp);
	}
	if ((noref == 0) && (inp->inp_in_hpts == 1)) {
		/* We are already on the hpts? */
		panic("%s: hpts:%p inp:%p already on the hpts?", __FUNCTION__, hpts, inp);
	}
#endif
	TAILQ_INSERT_TAIL(head, inp, inp_hpts);
	inp->inp_in_hpts = 1;
	hpts->p_on_queue_cnt++;
	if (noref == 0) {
		in_pcbref(inp);
	}
}

static inline void
hpts_sane_input_remove(struct tcp_hpts_entry *hpts, struct inpcb *inp, int clear)
{
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx) == 0) {
		/* We don't own the mutex? */
		panic("%s: hpts:%p inp:%p no hpts mutex", __FUNCTION__, hpts, inp);
	}
	if (hpts->p_cpu != inp->inp_input_cpu) {
		/* It is not the right cpu/mutex? */
		panic("%s: hpts:%p inp:%p incorrect CPU", __FUNCTION__, hpts, inp);
	}
	if (inp->inp_in_input == 0) {
		/* We are not on the input hpts? */
		panic("%s: hpts:%p inp:%p not on the input hpts?", __FUNCTION__, hpts, inp);
	}
#endif
	TAILQ_REMOVE(&hpts->p_input, inp, inp_input);
	hpts->p_on_inqueue_cnt--;
	if (hpts->p_on_inqueue_cnt < 0) {
#ifdef INVARIANTS
		panic("Hpts in goes negative inp:%p hpts:%p",
		    inp, hpts);
#endif
		hpts->p_on_inqueue_cnt = 0;
	}
#ifdef INVARIANTS
	if (TAILQ_EMPTY(&hpts->p_input) &&
	    (hpts->p_on_inqueue_cnt != 0)) {
		/* We should not be empty with a queue count */
		panic("%s hpts:%p in_hpts input empty but cnt:%d",
		    __FUNCTION__, hpts, hpts->p_on_inqueue_cnt);
	}
#endif
	if (clear)
		inp->inp_in_input = 0;
}

static inline void
hpts_sane_input_insert(struct tcp_hpts_entry *hpts, struct inpcb *inp, int line)
{
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx) == 0) {
		/* We don't own the mutex? */
		panic("%s: hpts:%p inp:%p no hpts mutex", __FUNCTION__, hpts, inp);
	}
	if (hpts->p_cpu != inp->inp_input_cpu) {
		/* It is not the right cpu/mutex? */
		panic("%s: hpts:%p inp:%p incorrect CPU", __FUNCTION__, hpts, inp);
	}
	if (inp->inp_in_input == 1) {
		/* We are already on the input hpts? */
		panic("%s: hpts:%p inp:%p already on the input hpts?", __FUNCTION__, hpts, inp);
	}
#endif
	TAILQ_INSERT_TAIL(&hpts->p_input, inp, inp_input);
	inp->inp_in_input = 1;
	hpts->p_on_inqueue_cnt++;
	in_pcbref(inp);
}

static int
sysctl_tcp_hpts_log(SYSCTL_HANDLER_ARGS)
{
	struct tcp_hpts_entry *hpts;
	size_t sz;
	int32_t logging_was, i;
	int32_t error = 0;

	/*
	 * HACK: Turn off logging so no locks are required this really needs
	 * a memory barrier :)
	 */
	logging_was = logging_on;
	logging_on = 0;
	if (!req->oldptr) {
		/* How much? */
		sz = 0;
		for (i = 0; i < tcp_pace.rp_num_hptss; i++) {
			hpts = tcp_pace.rp_ent[i];
			if (hpts->p_log == NULL)
				continue;
			sz += (sizeof(struct hpts_log) * hpts->p_logsize);
		}
		error = SYSCTL_OUT(req, 0, sz);
	} else {
		for (i = 0; i < tcp_pace.rp_num_hptss; i++) {
			hpts = tcp_pace.rp_ent[i];
			if (hpts->p_log == NULL)
				continue;
			if (hpts->p_log_wrapped)
				sz = (sizeof(struct hpts_log) * hpts->p_logsize);
			else
				sz = (sizeof(struct hpts_log) * hpts->p_log_at);
			error = SYSCTL_OUT(req, hpts->p_log, sz);
		}
	}
	logging_on = logging_was;
	return error;
}

SYSCTL_PROC(_net_inet_tcp_hpts, OID_AUTO, log, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
    0, 0, sysctl_tcp_hpts_log, "A", "tcp hptsi log");


static void
tcp_wakehpts(struct tcp_hpts_entry *hpts)
{
	HPTS_MTX_ASSERT(hpts);
	swi_sched(hpts->ie_cookie, 0);
	if (hpts->p_hpts_active == 2) {
		/* Rare sleeping on a ENOBUF */
		wakeup_one(hpts);
	}
}

static void
tcp_wakeinput(struct tcp_hpts_entry *hpts)
{
	HPTS_MTX_ASSERT(hpts);
	swi_sched(hpts->ie_cookie, 0);
	if (hpts->p_hpts_active == 2) {
		/* Rare sleeping on a ENOBUF */
		wakeup_one(hpts);
	}
}

struct tcp_hpts_entry *
tcp_cur_hpts(struct inpcb *inp)
{
	int32_t hpts_num;
	struct tcp_hpts_entry *hpts;

	hpts_num = inp->inp_hpts_cpu;
	hpts = tcp_pace.rp_ent[hpts_num];
	return (hpts);
}

struct tcp_hpts_entry *
tcp_hpts_lock(struct inpcb *inp)
{
	struct tcp_hpts_entry *hpts;
	int32_t hpts_num;

again:
	hpts_num = inp->inp_hpts_cpu;
	hpts = tcp_pace.rp_ent[hpts_num];
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx)) {
		panic("Hpts:%p owns mtx prior-to lock line:%d",
		    hpts, __LINE__);
	}
#endif
	mtx_lock(&hpts->p_mtx);
	if (hpts_num != inp->inp_hpts_cpu) {
		mtx_unlock(&hpts->p_mtx);
		goto again;
	}
	return (hpts);
}

struct tcp_hpts_entry *
tcp_input_lock(struct inpcb *inp)
{
	struct tcp_hpts_entry *hpts;
	int32_t hpts_num;

again:
	hpts_num = inp->inp_input_cpu;
	hpts = tcp_pace.rp_ent[hpts_num];
#ifdef INVARIANTS
	if (mtx_owned(&hpts->p_mtx)) {
		panic("Hpts:%p owns mtx prior-to lock line:%d",
		    hpts, __LINE__);
	}
#endif
	mtx_lock(&hpts->p_mtx);
	if (hpts_num != inp->inp_input_cpu) {
		mtx_unlock(&hpts->p_mtx);
		goto again;
	}
	return (hpts);
}

static void
tcp_remove_hpts_ref(struct inpcb *inp, struct tcp_hpts_entry *hpts, int line)
{
	int32_t add_freed;

	if (inp->inp_flags2 & INP_FREED) {
		/*
		 * Need to play a special trick so that in_pcbrele_wlocked
		 * does not return 1 when it really should have returned 0.
		 */
		add_freed = 1;
		inp->inp_flags2 &= ~INP_FREED;
	} else {
		add_freed = 0;
	}
#ifndef INP_REF_DEBUG
	if (in_pcbrele_wlocked(inp)) {
		/*
		 * This should not happen. We have the inpcb referred to by
		 * the main socket (why we are called) and the hpts. It
		 * should always return 0.
		 */
		panic("inpcb:%p release ret 1",
		    inp);
	}
#else
	if (__in_pcbrele_wlocked(inp, line)) {
		/*
		 * This should not happen. We have the inpcb referred to by
		 * the main socket (why we are called) and the hpts. It
		 * should always return 0.
		 */
		panic("inpcb:%p release ret 1",
		    inp);
	}
#endif
	if (add_freed) {
		inp->inp_flags2 |= INP_FREED;
	}
}

static void
tcp_hpts_remove_locked_output(struct tcp_hpts_entry *hpts, struct inpcb *inp, int32_t flags, int32_t line)
{
	if (inp->inp_in_hpts) {
		hpts_sane_pace_remove(hpts, inp, &hpts->p_hptss[inp->inp_hptsslot], 1);
		tcp_remove_hpts_ref(inp, hpts, line);
	}
}

static void
tcp_hpts_remove_locked_input(struct tcp_hpts_entry *hpts, struct inpcb *inp, int32_t flags, int32_t line)
{
	HPTS_MTX_ASSERT(hpts);
	if (inp->inp_in_input) {
		hpts_sane_input_remove(hpts, inp, 1);
		tcp_remove_hpts_ref(inp, hpts, line);
	}
}

/*
 * Called normally with the INP_LOCKED but it
 * does not matter, the hpts lock is the key
 * but the lock order allows us to hold the
 * INP lock and then get the hpts lock.
 *
 * Valid values in the flags are
 * HPTS_REMOVE_OUTPUT - remove from the output of the hpts.
 * HPTS_REMOVE_INPUT - remove from the input of the hpts.
 * Note that you can or both values together and get two
 * actions.
 */
void
__tcp_hpts_remove(struct inpcb *inp, int32_t flags, int32_t line)
{
	struct tcp_hpts_entry *hpts;

	INP_WLOCK_ASSERT(inp);
	if (flags & HPTS_REMOVE_OUTPUT) {
		hpts = tcp_hpts_lock(inp);
		tcp_hpts_remove_locked_output(hpts, inp, flags, line);
		mtx_unlock(&hpts->p_mtx);
	}
	if (flags & HPTS_REMOVE_INPUT) {
		hpts = tcp_input_lock(inp);
		tcp_hpts_remove_locked_input(hpts, inp, flags, line);
		mtx_unlock(&hpts->p_mtx);
	}
}

static inline int
hpts_tick(struct tcp_hpts_entry *hpts, int32_t plus)
{
	return ((hpts->p_prevtick + plus) % NUM_OF_HPTSI_SLOTS);
}

static int
tcp_queue_to_hpts_immediate_locked(struct inpcb *inp, struct tcp_hpts_entry *hpts, int32_t line, int32_t noref)
{
	int32_t need_wake = 0;
	uint32_t ticknow = 0;

	HPTS_MTX_ASSERT(hpts);
	if (inp->inp_in_hpts == 0) {
		/* Ok we need to set it on the hpts in the current slot */
		if (hpts->p_hpts_active == 0) {
			/* A sleeping hpts we want in next slot to run */
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_INSERT_SLEEPER, 0,
				    hpts_tick(hpts, 1));
			}
			inp->inp_hptsslot = hpts_tick(hpts, 1);
			inp->inp_hpts_request = 0;
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_SLEEP_BEFORE, 1, ticknow);
			}
			need_wake = 1;
		} else if ((void *)inp == hpts->p_inp) {
			/*
			 * We can't allow you to go into the same slot we
			 * are in. We must put you out.
			 */
			inp->inp_hptsslot = hpts->p_nxt_slot;
		} else
			inp->inp_hptsslot = hpts->p_cur_slot;
		hpts_sane_pace_insert(hpts, inp, &hpts->p_hptss[inp->inp_hptsslot], line, noref);
		inp->inp_hpts_request = 0;
		if (logging_on) {
			tcp_hpts_log_it(hpts, inp, HPTSLOG_IMMEDIATE, 0, 0);
		}
		if (need_wake) {
			/*
			 * Activate the hpts if it is sleeping and its
			 * timeout is not 1.
			 */
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_WAKEUP_HPTS, 0, ticknow);
			}
			hpts->p_direct_wake = 1;
			tcp_wakehpts(hpts);
		}
	}
	return (need_wake);
}

int
__tcp_queue_to_hpts_immediate(struct inpcb *inp, int32_t line)
{
	int32_t ret;
	struct tcp_hpts_entry *hpts;

	INP_WLOCK_ASSERT(inp);
	hpts = tcp_hpts_lock(inp);
	ret = tcp_queue_to_hpts_immediate_locked(inp, hpts, line, 0);
	mtx_unlock(&hpts->p_mtx);
	return (ret);
}

static void
tcp_hpts_insert_locked(struct tcp_hpts_entry *hpts, struct inpcb *inp, uint32_t slot, uint32_t cts, int32_t line,
    struct hpts_diag *diag, int32_t noref)
{
	int32_t need_new_to = 0;
	int32_t need_wakeup = 0;
	uint32_t largest_slot;
	uint32_t ticknow = 0;
	uint32_t slot_calc;

	HPTS_MTX_ASSERT(hpts);
	if (diag) {
		memset(diag, 0, sizeof(struct hpts_diag));
		diag->p_hpts_active = hpts->p_hpts_active;
		diag->p_nxt_slot = hpts->p_nxt_slot;
		diag->p_cur_slot = hpts->p_cur_slot;
		diag->slot_req = slot;
	}
	if ((inp->inp_in_hpts == 0) || noref) {
		inp->inp_hpts_request = slot;
		if (slot == 0) {
			/* Immediate */
			tcp_queue_to_hpts_immediate_locked(inp, hpts, line, noref);
			return;
		}
		if (hpts->p_hpts_active) {
			/*
			 * Its slot - 1 since nxt_slot is the next tick that
			 * will go off since the hpts is awake
			 */
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_INSERT_NORMAL, slot, 0);
			}
			/*
			 * We want to make sure that we don't place a inp in
			 * the range of p_cur_slot <-> p_nxt_slot. If we
			 * take from p_nxt_slot to the end, plus p_cur_slot
			 * and then take away 2, we will know how many is
			 * the max slots we can use.
			 */
			if (hpts->p_nxt_slot > hpts->p_cur_slot) {
				/*
				 * Non-wrap case nxt_slot <-> cur_slot we
				 * don't want to land in. So the diff gives
				 * us what is taken away from the number of
				 * slots.
				 */
				largest_slot = NUM_OF_HPTSI_SLOTS - (hpts->p_nxt_slot - hpts->p_cur_slot);
			} else if (hpts->p_nxt_slot == hpts->p_cur_slot) {
				largest_slot = NUM_OF_HPTSI_SLOTS - 2;
			} else {
				/*
				 * Wrap case so the diff gives us the number
				 * of slots that we can land in.
				 */
				largest_slot = hpts->p_cur_slot - hpts->p_nxt_slot;
			}
			/*
			 * We take away two so we never have a problem (20
			 * usec's) out of 1024000 usecs
			 */
			largest_slot -= 2;
			if (inp->inp_hpts_request > largest_slot) {
				/*
				 * Restrict max jump of slots and remember
				 * leftover
				 */
				slot = largest_slot;
				inp->inp_hpts_request -= largest_slot;
			} else {
				/* This one will run when we hit it */
				inp->inp_hpts_request = 0;
			}
			if (hpts->p_nxt_slot == hpts->p_cur_slot)
				slot_calc = (hpts->p_nxt_slot + slot) % NUM_OF_HPTSI_SLOTS;
			else
				slot_calc = (hpts->p_nxt_slot + slot - 1) % NUM_OF_HPTSI_SLOTS;
			if (slot_calc == hpts->p_cur_slot) {
#ifdef INVARIANTS
				/* TSNH */
				panic("Hpts:%p impossible slot calculation slot_calc:%u slot:%u largest:%u\n",
				    hpts, slot_calc, slot, largest_slot);
#endif
				if (slot_calc)
					slot_calc--;
				else
					slot_calc = NUM_OF_HPTSI_SLOTS - 1;
			}
			inp->inp_hptsslot = slot_calc;
			if (diag) {
				diag->inp_hptsslot = inp->inp_hptsslot;
			}
		} else {
			/*
			 * The hpts is sleeping, we need to figure out where
			 * it will wake up at and if we need to reschedule
			 * its time-out.
			 */
			uint32_t have_slept, yet_to_sleep;
			uint32_t slot_now;
			struct timeval tv;

			ticknow = tcp_gethptstick(&tv);
			slot_now = ticknow % NUM_OF_HPTSI_SLOTS;
			/*
			 * The user wants to be inserted at (slot_now +
			 * slot) % NUM_OF_HPTSI_SLOTS, so lets set that up.
			 */
			largest_slot = NUM_OF_HPTSI_SLOTS - 2;
			if (inp->inp_hpts_request > largest_slot) {
				/* Adjust the residual in inp_hpts_request */
				slot = largest_slot;
				inp->inp_hpts_request -= largest_slot;
			} else {
				/* No residual it all fits */
				inp->inp_hpts_request = 0;
			}
			inp->inp_hptsslot = (slot_now + slot) % NUM_OF_HPTSI_SLOTS;
			if (diag) {
				diag->slot_now = slot_now;
				diag->inp_hptsslot = inp->inp_hptsslot;
				diag->p_on_min_sleep = hpts->p_on_min_sleep;
			}
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_INSERT_SLEEPER, slot, ticknow);
			}
			/* Now do we need to restart the hpts's timer? */
			if (TSTMP_GT(ticknow, hpts->p_curtick))
				have_slept = ticknow - hpts->p_curtick;
			else
				have_slept = 0;
			if (have_slept < hpts->p_hpts_sleep_time) {
				/* This should be what happens */
				yet_to_sleep = hpts->p_hpts_sleep_time - have_slept;
			} else {
				/* We are over-due */
				yet_to_sleep = 0;
				need_wakeup = 1;
			}
			if (diag) {
				diag->have_slept = have_slept;
				diag->yet_to_sleep = yet_to_sleep;
				diag->hpts_sleep_time = hpts->p_hpts_sleep_time;
			}
			if ((hpts->p_on_min_sleep == 0) && (yet_to_sleep > slot)) {
				/*
				 * We need to reschedule the hptss time-out.
				 */
				hpts->p_hpts_sleep_time = slot;
				need_new_to = slot * HPTS_TICKS_PER_USEC;
			}
		}
		hpts_sane_pace_insert(hpts, inp, &hpts->p_hptss[inp->inp_hptsslot], line, noref);
		if (logging_on) {
			tcp_hpts_log_it(hpts, inp, HPTSLOG_INSERTED, slot, ticknow);
		}
		/*
		 * Now how far is the hpts sleeping to? if active is 1, its
		 * up and ticking we do nothing, otherwise we may need to
		 * reschedule its callout if need_new_to is set from above.
		 */
		if (need_wakeup) {
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_RESCHEDULE, 1, 0);
			}
			hpts->p_direct_wake = 1;
			tcp_wakehpts(hpts);
			if (diag) {
				diag->need_new_to = 0;
				diag->co_ret = 0xffff0000;
			}
		} else if (need_new_to) {
			int32_t co_ret;
			struct timeval tv;
			sbintime_t sb;

			tv.tv_sec = 0;
			tv.tv_usec = 0;
			while (need_new_to > HPTS_USEC_IN_SEC) {
				tv.tv_sec++;
				need_new_to -= HPTS_USEC_IN_SEC;
			}
			tv.tv_usec = need_new_to;
			sb = tvtosbt(tv);
			if (tcp_hpts_callout_skip_swi == 0) {
				co_ret = callout_reset_sbt_on(&hpts->co, sb, 0,
				    hpts_timeout_swi, hpts, hpts->p_cpu,
				    (C_DIRECT_EXEC | C_PREL(tcp_hpts_precision)));
			} else {
				co_ret = callout_reset_sbt_on(&hpts->co, sb, 0,
				    hpts_timeout_dir, hpts,
				    hpts->p_cpu,
				    C_PREL(tcp_hpts_precision));
			}
			if (diag) {
				diag->need_new_to = need_new_to;
				diag->co_ret = co_ret;
			}
		}
	} else {
#ifdef INVARIANTS
		panic("Hpts:%p tp:%p already on hpts and add?", hpts, inp);
#endif
	}
}

uint32_t
tcp_hpts_insert_diag(struct inpcb *inp, uint32_t slot, int32_t line, struct hpts_diag *diag){
	struct tcp_hpts_entry *hpts;
	uint32_t slot_on, cts;
	struct timeval tv;

	/*
	 * We now return the next-slot the hpts will be on, beyond its
	 * current run (if up) or where it was when it stopped if it is
	 * sleeping.
	 */
	INP_WLOCK_ASSERT(inp);
	hpts = tcp_hpts_lock(inp);
	if (in_ts_percision)
		microuptime(&tv);
	else
		getmicrouptime(&tv);
	cts = tcp_tv_to_usectick(&tv);
	tcp_hpts_insert_locked(hpts, inp, slot, cts, line, diag, 0);
	slot_on = hpts->p_nxt_slot;
	mtx_unlock(&hpts->p_mtx);
	return (slot_on);
}

uint32_t
__tcp_hpts_insert(struct inpcb *inp, uint32_t slot, int32_t line){
	return (tcp_hpts_insert_diag(inp, slot, line, NULL));
}

int
__tcp_queue_to_input_locked(struct inpcb *inp, struct tcp_hpts_entry *hpts, int32_t line)
{
	int32_t retval = 0;

	HPTS_MTX_ASSERT(hpts);
	if (inp->inp_in_input == 0) {
		/* Ok we need to set it on the hpts in the current slot */
		hpts_sane_input_insert(hpts, inp, line);
		retval = 1;
		if (hpts->p_hpts_active == 0) {
			/*
			 * Activate the hpts if it is sleeping.
			 */
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_WAKEUP_INPUT, 0, 0);
			}
			retval = 2;
			hpts->p_direct_wake = 1;
			tcp_wakeinput(hpts);
		}
	} else if (hpts->p_hpts_active == 0) {
		retval = 4;
		hpts->p_direct_wake = 1;
		tcp_wakeinput(hpts);
	}
	return (retval);
}

void
tcp_queue_pkt_to_input(struct tcpcb *tp, struct mbuf *m, struct tcphdr *th,
    int32_t tlen, int32_t drop_hdrlen, uint8_t iptos)
{
	/* Setup packet for input first */
	INP_WLOCK_ASSERT(tp->t_inpcb);
	m->m_pkthdr.pace_thoff = (uint16_t) ((caddr_t)th - mtod(m, caddr_t));
	m->m_pkthdr.pace_tlen = (uint16_t) tlen;
	m->m_pkthdr.pace_drphdrlen = drop_hdrlen;
	m->m_pkthdr.pace_tos = iptos;
	m->m_pkthdr.pace_lock = (curthread->td_epochnest != 0);
	if (tp->t_in_pkt == NULL) {
		tp->t_in_pkt = m;
		tp->t_tail_pkt = m;
	} else {
		tp->t_tail_pkt->m_nextpkt = m;
		tp->t_tail_pkt = m;
	}
}


int32_t
__tcp_queue_to_input(struct tcpcb *tp, struct mbuf *m, struct tcphdr *th,
    int32_t tlen, int32_t drop_hdrlen, uint8_t iptos, int32_t line){
	struct tcp_hpts_entry *hpts;
	int32_t ret;

	tcp_queue_pkt_to_input(tp, m, th, tlen, drop_hdrlen, iptos);
	hpts = tcp_input_lock(tp->t_inpcb);
	ret = __tcp_queue_to_input_locked(tp->t_inpcb, hpts, line);
	mtx_unlock(&hpts->p_mtx);
	return (ret);
}

void
__tcp_set_inp_to_drop(struct inpcb *inp, uint16_t reason, int32_t line)
{
	struct tcp_hpts_entry *hpts;
	struct tcpcb *tp;

	tp = intotcpcb(inp);
	hpts = tcp_input_lock(tp->t_inpcb);
	if (inp->inp_in_input == 0) {
		/* Ok we need to set it on the hpts in the current slot */
		hpts_sane_input_insert(hpts, inp, line);
		if (hpts->p_hpts_active == 0) {
			/*
			 * Activate the hpts if it is sleeping.
			 */
			hpts->p_direct_wake = 1;
			tcp_wakeinput(hpts);
		}
	} else if (hpts->p_hpts_active == 0) {
		hpts->p_direct_wake = 1;
		tcp_wakeinput(hpts);
	}
	inp->inp_hpts_drop_reas = reason;
	mtx_unlock(&hpts->p_mtx);
}

static uint16_t
hpts_random_cpu(struct inpcb *inp){
	/*
	 * No flow type set distribute the load randomly.
	 */
	uint16_t cpuid;
	uint32_t ran;

	/*
	 * If one has been set use it i.e. we want both in and out on the
	 * same hpts.
	 */
	if (inp->inp_input_cpu_set) {
		return (inp->inp_input_cpu);
	} else if (inp->inp_hpts_cpu_set) {
		return (inp->inp_hpts_cpu);
	}
	/* Nothing set use a random number */
	ran = arc4random();
	cpuid = (ran & 0xffff) % mp_ncpus;
	return (cpuid);
}

static uint16_t
hpts_cpuid(struct inpcb *inp){
	u_int cpuid;


	/*
	 * If one has been set use it i.e. we want both in and out on the
	 * same hpts.
	 */
	if (inp->inp_input_cpu_set) {
		return (inp->inp_input_cpu);
	} else if (inp->inp_hpts_cpu_set) {
		return (inp->inp_hpts_cpu);
	}
	/* If one is set the other must be the same */
#ifdef	RSS
	cpuid = rss_hash2cpuid(inp->inp_flowid, inp->inp_flowtype);
	if (cpuid == NETISR_CPUID_NONE)
		return (hpts_random_cpu(inp));
	else
		return (cpuid);
#else
	/*
	 * We don't have a flowid -> cpuid mapping, so cheat and just map
	 * unknown cpuids to curcpu.  Not the best, but apparently better
	 * than defaulting to swi 0.
	 */
	if (inp->inp_flowtype != M_HASHTYPE_NONE) {
		cpuid = inp->inp_flowid % mp_ncpus;
		return (cpuid);
	}
	cpuid = hpts_random_cpu(inp);
	return (cpuid);
#endif
}

/*
 * Do NOT try to optimize the processing of inp's
 * by first pulling off all the inp's into a temporary
 * list (e.g. TAILQ_CONCAT). If you do that the subtle
 * interactions of switching CPU's will kill because of
 * problems in the linked list manipulation. Basically
 * you would switch cpu's with the hpts mutex locked
 * but then while you were processing one of the inp's
 * some other one that you switch will get a new
 * packet on the different CPU. It will insert it
 * on the new hptss input list. Creating a temporary
 * link in the inp will not fix it either, since
 * the other hpts will be doing the same thing and
 * you will both end up using the temporary link.
 *
 * You will die in an ASSERT for tailq corruption if you
 * run INVARIANTS or you will die horribly without
 * INVARIANTS in some unknown way with a corrupt linked
 * list.
 */
static void
tcp_input_data(struct tcp_hpts_entry *hpts, struct timeval *tv)
{
	struct mbuf *m, *n;
	struct tcpcb *tp;
	struct inpcb *inp;
	uint16_t drop_reason;
	int16_t set_cpu;
	uint32_t did_prefetch = 0;
	int32_t ti_locked = TI_UNLOCKED;
	struct epoch_tracker et;

	HPTS_MTX_ASSERT(hpts);
	while ((inp = TAILQ_FIRST(&hpts->p_input)) != NULL) {
		HPTS_MTX_ASSERT(hpts);
		hpts_sane_input_remove(hpts, inp, 0);
		if (inp->inp_input_cpu_set == 0) {
			set_cpu = 1;
		} else {
			set_cpu = 0;
		}
		hpts->p_inp = inp;
		drop_reason = inp->inp_hpts_drop_reas;
		inp->inp_in_input = 0;
		mtx_unlock(&hpts->p_mtx);
		CURVNET_SET(inp->inp_vnet);
		if (drop_reason) {
			INP_INFO_RLOCK_ET(&V_tcbinfo, et);
			ti_locked = TI_RLOCKED;
		} else {
			ti_locked = TI_UNLOCKED;
		}
		INP_WLOCK(inp);
		if ((inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) ||
		    (inp->inp_flags2 & INP_FREED)) {
out:
			hpts->p_inp = NULL;
			if (ti_locked == TI_RLOCKED) {
				INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
			}
			if (in_pcbrele_wlocked(inp) == 0) {
				INP_WUNLOCK(inp);
			}
			ti_locked = TI_UNLOCKED;
			CURVNET_RESTORE();
			mtx_lock(&hpts->p_mtx);
			continue;
		}
		tp = intotcpcb(inp);
		if ((tp == NULL) || (tp->t_inpcb == NULL)) {
			goto out;
		}
		if (drop_reason) {
			/* This tcb is being destroyed for drop_reason */
			m = tp->t_in_pkt;
			if (m)
				n = m->m_nextpkt;
			else
				n = NULL;
			tp->t_in_pkt = NULL;
			while (m) {
				m_freem(m);
				m = n;
				if (m)
					n = m->m_nextpkt;
			}
			tp = tcp_drop(tp, drop_reason);
			INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
			if (tp == NULL) {
				INP_WLOCK(inp);
			}
			if (in_pcbrele_wlocked(inp) == 0)
				INP_WUNLOCK(inp);
			CURVNET_RESTORE();
			mtx_lock(&hpts->p_mtx);
			continue;
		}
		if (set_cpu) {
			/*
			 * Setup so the next time we will move to the right
			 * CPU. This should be a rare event. It will
			 * sometimes happens when we are the client side
			 * (usually not the server). Somehow tcp_output()
			 * gets called before the tcp_do_segment() sets the
			 * intial state. This means the r_cpu and r_hpts_cpu
			 * is 0. We get on the hpts, and then tcp_input()
			 * gets called setting up the r_cpu to the correct
			 * value. The hpts goes off and sees the mis-match.
			 * We simply correct it here and the CPU will switch
			 * to the new hpts nextime the tcb gets added to the
			 * the hpts (not this time) :-)
			 */
			tcp_set_hpts(inp);
		}
		m = tp->t_in_pkt;
		n = NULL;
		if (m != NULL &&
		    (m->m_pkthdr.pace_lock == TI_RLOCKED ||
		    tp->t_state != TCPS_ESTABLISHED)) {
			ti_locked = TI_RLOCKED;
			INP_INFO_RLOCK_ET(&V_tcbinfo, et);
			m = tp->t_in_pkt;
		}
		if (in_newts_every_tcb) {
			if (in_ts_percision)
				microuptime(tv);
			else
				getmicrouptime(tv);
		}
		if (tp->t_fb_ptr != NULL) {
			kern_prefetch(tp->t_fb_ptr, &did_prefetch);
			did_prefetch = 1;
		}
		/* Any input work to do, if so do it first */
		if ((m != NULL) && (m == tp->t_in_pkt)) {
			struct tcphdr *th;
			int32_t tlen, drop_hdrlen, nxt_pkt;
			uint8_t iptos;

			n = m->m_nextpkt;
			tp->t_in_pkt = tp->t_tail_pkt = NULL;
			while (m) {
				th = (struct tcphdr *)(mtod(m, caddr_t)+m->m_pkthdr.pace_thoff);
				tlen = m->m_pkthdr.pace_tlen;
				drop_hdrlen = m->m_pkthdr.pace_drphdrlen;
				iptos = m->m_pkthdr.pace_tos;
				m->m_nextpkt = NULL;
				if (n)
					nxt_pkt = 1;
				else
					nxt_pkt = 0;
				inp->inp_input_calls = 1;
				if (tp->t_fb->tfb_tcp_hpts_do_segment) {
					/* Use the hpts specific do_segment */
					(*tp->t_fb->tfb_tcp_hpts_do_segment) (m, th, inp->inp_socket,
					    tp, drop_hdrlen,
					    tlen, iptos, nxt_pkt, tv);
				} else {
					/* Use the default do_segment */
					(*tp->t_fb->tfb_tcp_do_segment) (m, th, inp->inp_socket,
					    tp, drop_hdrlen,
						tlen, iptos);
				}
				if (ti_locked == TI_RLOCKED)
					INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
				/*
				 * Do segment returns unlocked we need the
				 * lock again but we also need some kasserts
				 * here.
				 */
				INP_INFO_WUNLOCK_ASSERT(&V_tcbinfo);
				INP_UNLOCK_ASSERT(inp);
				m = n;
				if (m)
					n = m->m_nextpkt;
				if (m != NULL &&
				    m->m_pkthdr.pace_lock == TI_RLOCKED) {
					INP_INFO_RLOCK_ET(&V_tcbinfo, et);
					ti_locked = TI_RLOCKED;
				} else
					ti_locked = TI_UNLOCKED;
				INP_WLOCK(inp);
				/*
				 * Since we have an opening here we must
				 * re-check if the tcb went away while we
				 * were getting the lock(s).
				 */
				if ((inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) ||
				    (inp->inp_flags2 & INP_FREED)) {
					while (m) {
						m_freem(m);
						m = n;
						if (m)
							n = m->m_nextpkt;
					}
					goto out;
				}
				/*
				 * Now that we hold the INP lock, check if
				 * we need to upgrade our lock.
				 */
				if (ti_locked == TI_UNLOCKED &&
				    (tp->t_state != TCPS_ESTABLISHED)) {
					ti_locked = TI_RLOCKED;
					INP_INFO_RLOCK_ET(&V_tcbinfo, et);
				}
			}	/** end while(m) */
		}		/** end if ((m != NULL)  && (m == tp->t_in_pkt)) */
		if (in_pcbrele_wlocked(inp) == 0)
			INP_WUNLOCK(inp);
		if (ti_locked == TI_RLOCKED)
			INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		INP_INFO_WUNLOCK_ASSERT(&V_tcbinfo);
		INP_UNLOCK_ASSERT(inp);
		ti_locked = TI_UNLOCKED;
		mtx_lock(&hpts->p_mtx);
		hpts->p_inp = NULL;
		CURVNET_RESTORE();
	}
}

static int
tcp_hpts_est_run(struct tcp_hpts_entry *hpts)
{
	int32_t ticks_to_run;

	if (hpts->p_prevtick && (SEQ_GT(hpts->p_curtick, hpts->p_prevtick))) {
		ticks_to_run = hpts->p_curtick - hpts->p_prevtick;
		if (ticks_to_run >= (NUM_OF_HPTSI_SLOTS - 1)) {
			ticks_to_run = NUM_OF_HPTSI_SLOTS - 2;
		}
	} else {
		if (hpts->p_prevtick == hpts->p_curtick) {
			/* This happens when we get woken up right away */
			return (-1);
		}
		ticks_to_run = 1;
	}
	/* Set in where we will be when we catch up */
	hpts->p_nxt_slot = (hpts->p_cur_slot + ticks_to_run) % NUM_OF_HPTSI_SLOTS;
	if (hpts->p_nxt_slot == hpts->p_cur_slot) {
		panic("Impossible math -- hpts:%p p_nxt_slot:%d p_cur_slot:%d ticks_to_run:%d",
		    hpts, hpts->p_nxt_slot, hpts->p_cur_slot, ticks_to_run);
	}
	return (ticks_to_run);
}

static void
tcp_hptsi(struct tcp_hpts_entry *hpts, struct timeval *ctick)
{
	struct tcpcb *tp;
	struct inpcb *inp = NULL, *ninp;
	struct timeval tv;
	int32_t ticks_to_run, i, error, tick_now, interum_tick;
	int32_t paced_cnt = 0;
	int32_t did_prefetch = 0;
	int32_t prefetch_ninp = 0;
	int32_t prefetch_tp = 0;
	uint32_t cts;
	int16_t set_cpu;

	HPTS_MTX_ASSERT(hpts);
	hpts->p_curtick = tcp_tv_to_hptstick(ctick);
	cts = tcp_tv_to_usectick(ctick);
	memcpy(&tv, ctick, sizeof(struct timeval));
	hpts->p_cur_slot = hpts_tick(hpts, 1);

	/* Figure out if we had missed ticks */
again:
	HPTS_MTX_ASSERT(hpts);
	ticks_to_run = tcp_hpts_est_run(hpts);
	if (!TAILQ_EMPTY(&hpts->p_input)) {
		tcp_input_data(hpts, &tv);
	}
#ifdef INVARIANTS
	if (TAILQ_EMPTY(&hpts->p_input) &&
	    (hpts->p_on_inqueue_cnt != 0)) {
		panic("tp:%p in_hpts input empty but cnt:%d",
		    hpts, hpts->p_on_inqueue_cnt);
	}
#endif
	HPTS_MTX_ASSERT(hpts);
	/* Reset the ticks to run and time if we need too */
	interum_tick = tcp_gethptstick(&tv);
	if (interum_tick != hpts->p_curtick) {
		/* Save off the new time we execute to */
		*ctick = tv;
		hpts->p_curtick = interum_tick;
		cts = tcp_tv_to_usectick(&tv);
		hpts->p_cur_slot = hpts_tick(hpts, 1);
		ticks_to_run = tcp_hpts_est_run(hpts);
	}
	if (ticks_to_run == -1) {
		goto no_run;
	}
	if (logging_on) {
		tcp_hpts_log_it(hpts, inp, HPTSLOG_SETTORUN, ticks_to_run, 0);
	}
	if (hpts->p_on_queue_cnt == 0) {
		goto no_one;
	}
	HPTS_MTX_ASSERT(hpts);
	for (i = 0; i < ticks_to_run; i++) {
		/*
		 * Calculate our delay, if there are no extra ticks there
		 * was not any
		 */
		hpts->p_delayed_by = (ticks_to_run - (i + 1)) * HPTS_TICKS_PER_USEC;
		HPTS_MTX_ASSERT(hpts);
		while ((inp = TAILQ_FIRST(&hpts->p_hptss[hpts->p_cur_slot])) != NULL) {
			/* For debugging */
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_HPTSI, ticks_to_run, i);
			}
			hpts->p_inp = inp;
			paced_cnt++;
			if (hpts->p_cur_slot != inp->inp_hptsslot) {
				panic("Hpts:%p inp:%p slot mis-aligned %u vs %u",
				    hpts, inp, hpts->p_cur_slot, inp->inp_hptsslot);
			}
			/* Now pull it */
			if (inp->inp_hpts_cpu_set == 0) {
				set_cpu = 1;
			} else {
				set_cpu = 0;
			}
			hpts_sane_pace_remove(hpts, inp, &hpts->p_hptss[hpts->p_cur_slot], 0);
			if ((ninp = TAILQ_FIRST(&hpts->p_hptss[hpts->p_cur_slot])) != NULL) {
				/* We prefetch the next inp if possible */
				kern_prefetch(ninp, &prefetch_ninp);
				prefetch_ninp = 1;
			}
			if (inp->inp_hpts_request) {
				/*
				 * This guy is deferred out further in time
				 * then our wheel had on it. Push him back
				 * on the wheel.
				 */
				int32_t remaining_slots;

				remaining_slots = ticks_to_run - (i + 1);
				if (inp->inp_hpts_request > remaining_slots) {
					/*
					 * Keep INVARIANTS happy by clearing
					 * the flag
					 */
					tcp_hpts_insert_locked(hpts, inp, inp->inp_hpts_request, cts, __LINE__, NULL, 1);
					hpts->p_inp = NULL;
					continue;
				}
				inp->inp_hpts_request = 0;
			}
			/*
			 * We clear the hpts flag here after dealing with
			 * remaining slots. This way anyone looking with the
			 * TCB lock will see its on the hpts until just
			 * before we unlock.
			 */
			inp->inp_in_hpts = 0;
			mtx_unlock(&hpts->p_mtx);
			INP_WLOCK(inp);
			if (in_pcbrele_wlocked(inp)) {
				mtx_lock(&hpts->p_mtx);
				if (logging_on)
					tcp_hpts_log_it(hpts, hpts->p_inp, HPTSLOG_INP_DONE, 0, 1);
				hpts->p_inp = NULL;
				continue;
			}
			if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
out_now:
#ifdef INVARIANTS
				if (mtx_owned(&hpts->p_mtx)) {
					panic("Hpts:%p owns mtx prior-to lock line:%d",
					    hpts, __LINE__);
				}
#endif
				INP_WUNLOCK(inp);
				mtx_lock(&hpts->p_mtx);
				if (logging_on)
					tcp_hpts_log_it(hpts, hpts->p_inp, HPTSLOG_INP_DONE, 0, 3);
				hpts->p_inp = NULL;
				continue;
			}
			tp = intotcpcb(inp);
			if ((tp == NULL) || (tp->t_inpcb == NULL)) {
				goto out_now;
			}
			if (set_cpu) {
				/*
				 * Setup so the next time we will move to
				 * the right CPU. This should be a rare
				 * event. It will sometimes happens when we
				 * are the client side (usually not the
				 * server). Somehow tcp_output() gets called
				 * before the tcp_do_segment() sets the
				 * intial state. This means the r_cpu and
				 * r_hpts_cpu is 0. We get on the hpts, and
				 * then tcp_input() gets called setting up
				 * the r_cpu to the correct value. The hpts
				 * goes off and sees the mis-match. We
				 * simply correct it here and the CPU will
				 * switch to the new hpts nextime the tcb
				 * gets added to the the hpts (not this one)
				 * :-)
				 */
				tcp_set_hpts(inp);
			}
			if (out_newts_every_tcb) {
				struct timeval sv;

				if (out_ts_percision)
					microuptime(&sv);
				else
					getmicrouptime(&sv);
				cts = tcp_tv_to_usectick(&sv);
			}
			CURVNET_SET(inp->inp_vnet);
			/*
			 * There is a hole here, we get the refcnt on the
			 * inp so it will still be preserved but to make
			 * sure we can get the INP we need to hold the p_mtx
			 * above while we pull out the tp/inp,  as long as
			 * fini gets the lock first we are assured of having
			 * a sane INP we can lock and test.
			 */
#ifdef INVARIANTS
			if (mtx_owned(&hpts->p_mtx)) {
				panic("Hpts:%p owns mtx before tcp-output:%d",
				    hpts, __LINE__);
			}
#endif
			if (tp->t_fb_ptr != NULL) {
				kern_prefetch(tp->t_fb_ptr, &did_prefetch);
				did_prefetch = 1;
			}
			inp->inp_hpts_calls = 1;
			if (tp->t_fb->tfb_tcp_output_wtime != NULL) {
				error = (*tp->t_fb->tfb_tcp_output_wtime) (tp, &tv);
			} else {
				error = tp->t_fb->tfb_tcp_output(tp);
			}
			if (ninp && ninp->inp_ppcb) {
				/*
				 * If we have a nxt inp, see if we can
				 * prefetch its ppcb. Note this may seem
				 * "risky" since we have no locks (other
				 * than the previous inp) and there no
				 * assurance that ninp was not pulled while
				 * we were processing inp and freed. If this
				 * occured it could mean that either:
				 *
				 * a) Its NULL (which is fine we won't go
				 * here) <or> b) Its valid (which is cool we
				 * will prefetch it) <or> c) The inp got
				 * freed back to the slab which was
				 * reallocated. Then the piece of memory was
				 * re-used and something else (not an
				 * address) is in inp_ppcb. If that occurs
				 * we don't crash, but take a TLB shootdown
				 * performance hit (same as if it was NULL
				 * and we tried to pre-fetch it).
				 *
				 * Considering that the likelyhood of <c> is
				 * quite rare we will take a risk on doing
				 * this. If performance drops after testing
				 * we can always take this out. NB: the
				 * kern_prefetch on amd64 actually has
				 * protection against a bad address now via
				 * the DMAP_() tests. This will prevent the
				 * TLB hit, and instead if <c> occurs just
				 * cause us to load cache with a useless
				 * address (to us).
				 */
				kern_prefetch(ninp->inp_ppcb, &prefetch_tp);
				prefetch_tp = 1;
			}
			INP_WUNLOCK(inp);
			INP_UNLOCK_ASSERT(inp);
			CURVNET_RESTORE();
#ifdef INVARIANTS
			if (mtx_owned(&hpts->p_mtx)) {
				panic("Hpts:%p owns mtx prior-to lock line:%d",
				    hpts, __LINE__);
			}
#endif
			mtx_lock(&hpts->p_mtx);
			if (logging_on)
				tcp_hpts_log_it(hpts, hpts->p_inp, HPTSLOG_INP_DONE, 0, 4);
			hpts->p_inp = NULL;
		}
		HPTS_MTX_ASSERT(hpts);
		hpts->p_inp = NULL;
		hpts->p_cur_slot++;
		if (hpts->p_cur_slot >= NUM_OF_HPTSI_SLOTS) {
			hpts->p_cur_slot = 0;
		}
	}
no_one:
	HPTS_MTX_ASSERT(hpts);
	hpts->p_prevtick = hpts->p_curtick;
	hpts->p_delayed_by = 0;
	/*
	 * Check to see if we took an excess amount of time and need to run
	 * more ticks (if we did not hit eno-bufs).
	 */
	/* Re-run any input that may be there */
	(void)tcp_gethptstick(&tv);
	if (!TAILQ_EMPTY(&hpts->p_input)) {
		tcp_input_data(hpts, &tv);
	}
#ifdef INVARIANTS
	if (TAILQ_EMPTY(&hpts->p_input) &&
	    (hpts->p_on_inqueue_cnt != 0)) {
		panic("tp:%p in_hpts input empty but cnt:%d",
		    hpts, hpts->p_on_inqueue_cnt);
	}
#endif
	tick_now = tcp_gethptstick(&tv);
	if (SEQ_GT(tick_now, hpts->p_prevtick)) {
		struct timeval res;

		/* Did we really spend a full tick or more in here? */
		timersub(&tv, ctick, &res);
		if (res.tv_sec || (res.tv_usec >= HPTS_TICKS_PER_USEC)) {
			counter_u64_add(hpts_loops, 1);
			if (logging_on) {
				tcp_hpts_log_it(hpts, inp, HPTSLOG_TOLONG, (uint32_t) res.tv_usec, tick_now);
			}
			*ctick = res;
			hpts->p_curtick = tick_now;
			goto again;
		}
	}
no_run:
	{
		uint32_t t = 0, i, fnd = 0;

		if (hpts->p_on_queue_cnt) {


			/*
			 * Find next slot that is occupied and use that to
			 * be the sleep time.
			 */
			for (i = 1, t = hpts->p_nxt_slot; i < NUM_OF_HPTSI_SLOTS; i++) {
				if (TAILQ_EMPTY(&hpts->p_hptss[t]) == 0) {
					fnd = 1;
					break;
				}
				t = (t + 1) % NUM_OF_HPTSI_SLOTS;
			}
			if (fnd) {
				hpts->p_hpts_sleep_time = i;
			} else {
				counter_u64_add(back_tosleep, 1);
#ifdef INVARIANTS
				panic("Hpts:%p cnt:%d but non found", hpts, hpts->p_on_queue_cnt);
#endif
				hpts->p_on_queue_cnt = 0;
				goto non_found;
			}
			t++;
		} else {
			/* No one on the wheel sleep for all but 2 slots  */
non_found:
			if (hpts_sleep_max == 0)
				hpts_sleep_max = 1;
			hpts->p_hpts_sleep_time = min((NUM_OF_HPTSI_SLOTS - 2), hpts_sleep_max);
			t = 0;
		}
		if (logging_on) {
			tcp_hpts_log_it(hpts, inp, HPTSLOG_SLEEPSET, t, (hpts->p_hpts_sleep_time * HPTS_TICKS_PER_USEC));
		}
	}
}

void
__tcp_set_hpts(struct inpcb *inp, int32_t line)
{
	struct tcp_hpts_entry *hpts;

	INP_WLOCK_ASSERT(inp);
	hpts = tcp_hpts_lock(inp);
	if ((inp->inp_in_hpts == 0) &&
	    (inp->inp_hpts_cpu_set == 0)) {
		inp->inp_hpts_cpu = hpts_cpuid(inp);
		inp->inp_hpts_cpu_set = 1;
	}
	mtx_unlock(&hpts->p_mtx);
	hpts = tcp_input_lock(inp);
	if ((inp->inp_input_cpu_set == 0) &&
	    (inp->inp_in_input == 0)) {
		inp->inp_input_cpu = hpts_cpuid(inp);
		inp->inp_input_cpu_set = 1;
	}
	mtx_unlock(&hpts->p_mtx);
}

uint16_t
tcp_hpts_delayedby(struct inpcb *inp){
	return (tcp_pace.rp_ent[inp->inp_hpts_cpu]->p_delayed_by);
}

static void
tcp_hpts_thread(void *ctx)
{
	struct tcp_hpts_entry *hpts;
	struct timeval tv;
	sbintime_t sb;

	hpts = (struct tcp_hpts_entry *)ctx;
	mtx_lock(&hpts->p_mtx);
	if (hpts->p_direct_wake) {
		/* Signaled by input */
		if (logging_on)
			tcp_hpts_log_it(hpts, NULL, HPTSLOG_AWAKE, 1, 1);
		callout_stop(&hpts->co);
	} else {
		/* Timed out */
		if (callout_pending(&hpts->co) ||
		    !callout_active(&hpts->co)) {
			if (logging_on)
				tcp_hpts_log_it(hpts, NULL, HPTSLOG_AWAKE, 2, 2);
			mtx_unlock(&hpts->p_mtx);
			return;
		}
		callout_deactivate(&hpts->co);
		if (logging_on)
			tcp_hpts_log_it(hpts, NULL, HPTSLOG_AWAKE, 3, 3);
	}
	hpts->p_hpts_active = 1;
	(void)tcp_gethptstick(&tv);
	tcp_hptsi(hpts, &tv);
	HPTS_MTX_ASSERT(hpts);
	tv.tv_sec = 0;
	tv.tv_usec = hpts->p_hpts_sleep_time * HPTS_TICKS_PER_USEC;
	if (tcp_min_hptsi_time && (tv.tv_usec < tcp_min_hptsi_time)) {
		tv.tv_usec = tcp_min_hptsi_time;
		hpts->p_on_min_sleep = 1;
	} else {
		/* Clear the min sleep flag */
		hpts->p_on_min_sleep = 0;
	}
	hpts->p_hpts_active = 0;
	sb = tvtosbt(tv);
	if (tcp_hpts_callout_skip_swi == 0) {
		callout_reset_sbt_on(&hpts->co, sb, 0,
		    hpts_timeout_swi, hpts, hpts->p_cpu,
		    (C_DIRECT_EXEC | C_PREL(tcp_hpts_precision)));
	} else {
		callout_reset_sbt_on(&hpts->co, sb, 0,
		    hpts_timeout_dir, hpts,
		    hpts->p_cpu,
		    C_PREL(tcp_hpts_precision));
	}
	hpts->p_direct_wake = 0;
	mtx_unlock(&hpts->p_mtx);
}

#undef	timersub

static void
tcp_init_hptsi(void *st)
{
	int32_t i, j, error, bound = 0, created = 0;
	size_t sz, asz;
	struct timeval tv;
	sbintime_t sb;
	struct tcp_hpts_entry *hpts;
	char unit[16];
	uint32_t ncpus = mp_ncpus ? mp_ncpus : MAXCPU;

	tcp_pace.rp_proc = NULL;
	tcp_pace.rp_num_hptss = ncpus;
	hpts_loops = counter_u64_alloc(M_WAITOK);
	back_tosleep = counter_u64_alloc(M_WAITOK);

	sz = (tcp_pace.rp_num_hptss * sizeof(struct tcp_hpts_entry *));
	tcp_pace.rp_ent = malloc(sz, M_TCPHPTS, M_WAITOK | M_ZERO);
	asz = sizeof(struct hptsh) * NUM_OF_HPTSI_SLOTS;
	for (i = 0; i < tcp_pace.rp_num_hptss; i++) {
		tcp_pace.rp_ent[i] = malloc(sizeof(struct tcp_hpts_entry),
		    M_TCPHPTS, M_WAITOK | M_ZERO);
		tcp_pace.rp_ent[i]->p_hptss = malloc(asz,
		    M_TCPHPTS, M_WAITOK);
		hpts = tcp_pace.rp_ent[i];
		/*
		 * Init all the hpts structures that are not specifically
		 * zero'd by the allocations. Also lets attach them to the
		 * appropriate sysctl block as well.
		 */
		mtx_init(&hpts->p_mtx, "tcp_hpts_lck",
		    "hpts", MTX_DEF | MTX_DUPOK);
		TAILQ_INIT(&hpts->p_input);
		for (j = 0; j < NUM_OF_HPTSI_SLOTS; j++) {
			TAILQ_INIT(&hpts->p_hptss[j]);
		}
		sysctl_ctx_init(&hpts->hpts_ctx);
		sprintf(unit, "%d", i);
		hpts->hpts_root = SYSCTL_ADD_NODE(&hpts->hpts_ctx,
		    SYSCTL_STATIC_CHILDREN(_net_inet_tcp_hpts),
		    OID_AUTO,
		    unit,
		    CTLFLAG_RW, 0,
		    "");
		SYSCTL_ADD_INT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "in_qcnt", CTLFLAG_RD,
		    &hpts->p_on_inqueue_cnt, 0,
		    "Count TCB's awaiting input processing");
		SYSCTL_ADD_INT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "out_qcnt", CTLFLAG_RD,
		    &hpts->p_on_queue_cnt, 0,
		    "Count TCB's awaiting output processing");
		SYSCTL_ADD_UINT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "active", CTLFLAG_RD,
		    &hpts->p_hpts_active, 0,
		    "Is the hpts active");
		SYSCTL_ADD_UINT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "curslot", CTLFLAG_RD,
		    &hpts->p_cur_slot, 0,
		    "What the current slot is if active");
		SYSCTL_ADD_UINT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "curtick", CTLFLAG_RD,
		    &hpts->p_curtick, 0,
		    "What the current tick on if active");
		SYSCTL_ADD_UINT(&hpts->hpts_ctx,
		    SYSCTL_CHILDREN(hpts->hpts_root),
		    OID_AUTO, "logsize", CTLFLAG_RD,
		    &hpts->p_logsize, 0,
		    "Hpts logging buffer size");
		hpts->p_hpts_sleep_time = NUM_OF_HPTSI_SLOTS - 2;
		hpts->p_num = i;
		hpts->p_prevtick = hpts->p_curtick = tcp_gethptstick(&tv);
		hpts->p_prevtick -= 1;
		hpts->p_prevtick %= NUM_OF_HPTSI_SLOTS;
		hpts->p_cpu = 0xffff;
		hpts->p_nxt_slot = 1;
		hpts->p_logsize = tcp_hpts_logging_size;
		if (hpts->p_logsize) {
			sz = (sizeof(struct hpts_log) * hpts->p_logsize);
			hpts->p_log = malloc(sz, M_TCPHPTS, M_WAITOK | M_ZERO);
		}
		callout_init(&hpts->co, 1);
	}
	/*
	 * Now lets start ithreads to handle the hptss.
	 */
	CPU_FOREACH(i) {
		hpts = tcp_pace.rp_ent[i];
		hpts->p_cpu = i;
		error = swi_add(&hpts->ie, "hpts",
		    tcp_hpts_thread, (void *)hpts,
		    SWI_NET, INTR_MPSAFE, &hpts->ie_cookie);
		if (error) {
			panic("Can't add hpts:%p i:%d err:%d",
			    hpts, i, error);
		}
		created++;
		if (tcp_bind_threads) {
			if (intr_event_bind(hpts->ie, i) == 0)
				bound++;
		}
		tv.tv_sec = 0;
		tv.tv_usec = hpts->p_hpts_sleep_time * HPTS_TICKS_PER_USEC;
		sb = tvtosbt(tv);
		if (tcp_hpts_callout_skip_swi == 0) {
			callout_reset_sbt_on(&hpts->co, sb, 0,
			    hpts_timeout_swi, hpts, hpts->p_cpu,
			    (C_DIRECT_EXEC | C_PREL(tcp_hpts_precision)));
		} else {
			callout_reset_sbt_on(&hpts->co, sb, 0,
			    hpts_timeout_dir, hpts,
			    hpts->p_cpu,
			    C_PREL(tcp_hpts_precision));
		}
	}
	printf("TCP Hpts created %d swi interrupt thread and bound %d\n",
	    created, bound);
	return;
}

SYSINIT(tcphptsi, SI_SUB_KTHREAD_IDLE, SI_ORDER_ANY, tcp_init_hptsi, NULL);
MODULE_VERSION(tcphpts, 1);
