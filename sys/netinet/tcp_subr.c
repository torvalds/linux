/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *	@(#)tcp_subr.c	8.2 (Berkeley) 5/24/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_tcpdebug.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/callout.h>
#include <sys/eventhandler.h>
#ifdef TCP_HHOOK
#include <sys/hhook.h>
#endif
#include <sys/kernel.h>
#ifdef TCP_HHOOK
#include <sys/khelp.h>
#endif
#include <sys/sysctl.h>
#include <sys/jail.h>
#include <sys/malloc.h>
#include <sys/refcount.h>
#include <sys/mbuf.h>
#ifdef INET6
#include <sys/domain.h>
#endif
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sdt.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/random.h>

#include <vm/uma.h>

#include <net/route.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip_var.h>
#ifdef INET6
#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet6/in6_fib.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/nd6.h>
#endif

#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_log_buf.h>
#include <netinet/tcp_syncache.h>
#include <netinet/tcp_hpts.h>
#include <netinet/cc/cc.h>
#ifdef INET6
#include <netinet6/tcp6_var.h>
#endif
#include <netinet/tcpip.h>
#include <netinet/tcp_fastopen.h>
#ifdef TCPPCAP
#include <netinet/tcp_pcap.h>
#endif
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif
#ifdef INET6
#include <netinet6/ip6protosw.h>
#endif
#ifdef TCP_OFFLOAD
#include <netinet/tcp_offload.h>
#endif

#include <netipsec/ipsec_support.h>

#include <machine/in_cksum.h>
#include <sys/md5.h>

#include <security/mac/mac_framework.h>

VNET_DEFINE(int, tcp_mssdflt) = TCP_MSS;
#ifdef INET6
VNET_DEFINE(int, tcp_v6mssdflt) = TCP6_MSS;
#endif

struct rwlock tcp_function_lock;

static int
sysctl_net_inet_tcp_mss_check(SYSCTL_HANDLER_ARGS)
{
	int error, new;

	new = V_tcp_mssdflt;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (new < TCP_MINMSS)
			error = EINVAL;
		else
			V_tcp_mssdflt = new;
	}
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, TCPCTL_MSSDFLT, mssdflt,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW, &VNET_NAME(tcp_mssdflt), 0,
    &sysctl_net_inet_tcp_mss_check, "I",
    "Default TCP Maximum Segment Size");

#ifdef INET6
static int
sysctl_net_inet_tcp_mss_v6_check(SYSCTL_HANDLER_ARGS)
{
	int error, new;

	new = V_tcp_v6mssdflt;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr) {
		if (new < TCP_MINMSS)
			error = EINVAL;
		else
			V_tcp_v6mssdflt = new;
	}
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, TCPCTL_V6MSSDFLT, v6mssdflt,
    CTLFLAG_VNET | CTLTYPE_INT | CTLFLAG_RW, &VNET_NAME(tcp_v6mssdflt), 0,
    &sysctl_net_inet_tcp_mss_v6_check, "I",
   "Default TCP Maximum Segment Size for IPv6");
#endif /* INET6 */

/*
 * Minimum MSS we accept and use. This prevents DoS attacks where
 * we are forced to a ridiculous low MSS like 20 and send hundreds
 * of packets instead of one. The effect scales with the available
 * bandwidth and quickly saturates the CPU and network interface
 * with packet generation and sending. Set to zero to disable MINMSS
 * checking. This setting prevents us from sending too small packets.
 */
VNET_DEFINE(int, tcp_minmss) = TCP_MINMSS;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, minmss, CTLFLAG_VNET | CTLFLAG_RW,
     &VNET_NAME(tcp_minmss), 0,
    "Minimum TCP Maximum Segment Size");

VNET_DEFINE(int, tcp_do_rfc1323) = 1;
SYSCTL_INT(_net_inet_tcp, TCPCTL_DO_RFC1323, rfc1323, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_do_rfc1323), 0,
    "Enable rfc1323 (high performance TCP) extensions");

static int	tcp_log_debug = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, log_debug, CTLFLAG_RW,
    &tcp_log_debug, 0, "Log errors caused by incoming TCP segments");

static int	tcp_tcbhashsize;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, tcbhashsize, CTLFLAG_RDTUN | CTLFLAG_NOFETCH,
    &tcp_tcbhashsize, 0, "Size of TCP control-block hashtable");

static int	do_tcpdrain = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, do_tcpdrain, CTLFLAG_RW, &do_tcpdrain, 0,
    "Enable tcp_drain routine for extra help when low on mbufs");

SYSCTL_UINT(_net_inet_tcp, OID_AUTO, pcbcount, CTLFLAG_VNET | CTLFLAG_RD,
    &VNET_NAME(tcbinfo.ipi_count), 0, "Number of active PCBs");

VNET_DEFINE_STATIC(int, icmp_may_rst) = 1;
#define	V_icmp_may_rst			VNET(icmp_may_rst)
SYSCTL_INT(_net_inet_tcp, OID_AUTO, icmp_may_rst, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(icmp_may_rst), 0,
    "Certain ICMP unreachable messages may abort connections in SYN_SENT");

VNET_DEFINE_STATIC(int, tcp_isn_reseed_interval) = 0;
#define	V_tcp_isn_reseed_interval	VNET(tcp_isn_reseed_interval)
SYSCTL_INT(_net_inet_tcp, OID_AUTO, isn_reseed_interval, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(tcp_isn_reseed_interval), 0,
    "Seconds between reseeding of ISN secret");

static int	tcp_soreceive_stream;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, soreceive_stream, CTLFLAG_RDTUN,
    &tcp_soreceive_stream, 0, "Using soreceive_stream for TCP sockets");

VNET_DEFINE(uma_zone_t, sack_hole_zone);
#define	V_sack_hole_zone		VNET(sack_hole_zone)

#ifdef TCP_HHOOK
VNET_DEFINE(struct hhook_head *, tcp_hhh[HHOOK_TCP_LAST+1]);
#endif

#define TS_OFFSET_SECRET_LENGTH 32
VNET_DEFINE_STATIC(u_char, ts_offset_secret[TS_OFFSET_SECRET_LENGTH]);
#define	V_ts_offset_secret	VNET(ts_offset_secret)

static int	tcp_default_fb_init(struct tcpcb *tp);
static void	tcp_default_fb_fini(struct tcpcb *tp, int tcb_is_purged);
static int	tcp_default_handoff_ok(struct tcpcb *tp);
static struct inpcb *tcp_notify(struct inpcb *, int);
static struct inpcb *tcp_mtudisc_notify(struct inpcb *, int);
static void tcp_mtudisc(struct inpcb *, int);
static char *	tcp_log_addr(struct in_conninfo *inc, struct tcphdr *th,
		    void *ip4hdr, const void *ip6hdr);


static struct tcp_function_block tcp_def_funcblk = {
	.tfb_tcp_block_name = "freebsd",
	.tfb_tcp_output = tcp_output,
	.tfb_tcp_do_segment = tcp_do_segment,
	.tfb_tcp_ctloutput = tcp_default_ctloutput,
	.tfb_tcp_handoff_ok = tcp_default_handoff_ok,
	.tfb_tcp_fb_init = tcp_default_fb_init,
	.tfb_tcp_fb_fini = tcp_default_fb_fini,
};

static int tcp_fb_cnt = 0;
struct tcp_funchead t_functions;
static struct tcp_function_block *tcp_func_set_ptr = &tcp_def_funcblk;

static struct tcp_function_block *
find_tcp_functions_locked(struct tcp_function_set *fs)
{
	struct tcp_function *f;
	struct tcp_function_block *blk=NULL;

	TAILQ_FOREACH(f, &t_functions, tf_next) {
		if (strcmp(f->tf_name, fs->function_set_name) == 0) {
			blk = f->tf_fb;
			break;
		}
	}
	return(blk);
}

static struct tcp_function_block *
find_tcp_fb_locked(struct tcp_function_block *blk, struct tcp_function **s)
{
	struct tcp_function_block *rblk=NULL;
	struct tcp_function *f;

	TAILQ_FOREACH(f, &t_functions, tf_next) {
		if (f->tf_fb == blk) {
			rblk = blk;
			if (s) {
				*s = f;
			}
			break;
		}
	}
	return (rblk);
}

struct tcp_function_block *
find_and_ref_tcp_functions(struct tcp_function_set *fs)
{
	struct tcp_function_block *blk;
	
	rw_rlock(&tcp_function_lock);	
	blk = find_tcp_functions_locked(fs);
	if (blk)
		refcount_acquire(&blk->tfb_refcnt); 
	rw_runlock(&tcp_function_lock);
	return(blk);
}

struct tcp_function_block *
find_and_ref_tcp_fb(struct tcp_function_block *blk)
{
	struct tcp_function_block *rblk;
	
	rw_rlock(&tcp_function_lock);	
	rblk = find_tcp_fb_locked(blk, NULL);
	if (rblk) 
		refcount_acquire(&rblk->tfb_refcnt);
	rw_runlock(&tcp_function_lock);
	return(rblk);
}

static struct tcp_function_block *
find_and_ref_tcp_default_fb(void)
{
	struct tcp_function_block *rblk;

	rw_rlock(&tcp_function_lock);
	rblk = tcp_func_set_ptr;
	refcount_acquire(&rblk->tfb_refcnt);
	rw_runlock(&tcp_function_lock);
	return (rblk);
}

void
tcp_switch_back_to_default(struct tcpcb *tp)
{
	struct tcp_function_block *tfb;

	KASSERT(tp->t_fb != &tcp_def_funcblk,
	    ("%s: called by the built-in default stack", __func__));

	/*
	 * Release the old stack. This function will either find a new one
	 * or panic.
	 */
	if (tp->t_fb->tfb_tcp_fb_fini != NULL)
		(*tp->t_fb->tfb_tcp_fb_fini)(tp, 0);
	refcount_release(&tp->t_fb->tfb_refcnt);

	/*
	 * Now, we'll find a new function block to use.
	 * Start by trying the current user-selected
	 * default, unless this stack is the user-selected
	 * default.
	 */
	tfb = find_and_ref_tcp_default_fb();
	if (tfb == tp->t_fb) {
		refcount_release(&tfb->tfb_refcnt);
		tfb = NULL;
	}
	/* Does the stack accept this connection? */
	if (tfb != NULL && tfb->tfb_tcp_handoff_ok != NULL &&
	    (*tfb->tfb_tcp_handoff_ok)(tp)) {
		refcount_release(&tfb->tfb_refcnt);
		tfb = NULL;
	}
	/* Try to use that stack. */
	if (tfb != NULL) {
		/* Initialize the new stack. If it succeeds, we are done. */
		tp->t_fb = tfb;
		if (tp->t_fb->tfb_tcp_fb_init == NULL ||
		    (*tp->t_fb->tfb_tcp_fb_init)(tp) == 0)
			return;

		/*
		 * Initialization failed. Release the reference count on
		 * the stack.
		 */
		refcount_release(&tfb->tfb_refcnt);
	}

	/*
	 * If that wasn't feasible, use the built-in default
	 * stack which is not allowed to reject anyone.
	 */
	tfb = find_and_ref_tcp_fb(&tcp_def_funcblk);
	if (tfb == NULL) {
		/* there always should be a default */
		panic("Can't refer to tcp_def_funcblk");
	}
	if (tfb->tfb_tcp_handoff_ok != NULL) {
		if ((*tfb->tfb_tcp_handoff_ok) (tp)) {
			/* The default stack cannot say no */
			panic("Default stack rejects a new session?");
		}
	}
	tp->t_fb = tfb;
	if (tp->t_fb->tfb_tcp_fb_init != NULL &&
	    (*tp->t_fb->tfb_tcp_fb_init)(tp)) {
		/* The default stack cannot fail */
		panic("Default stack initialization failed");
	}
}

static int
sysctl_net_inet_default_tcp_functions(SYSCTL_HANDLER_ARGS)
{
	int error=ENOENT;
	struct tcp_function_set fs;
	struct tcp_function_block *blk;

	memset(&fs, 0, sizeof(fs));
	rw_rlock(&tcp_function_lock);
	blk = find_tcp_fb_locked(tcp_func_set_ptr, NULL);
	if (blk) {
		/* Found him */
		strcpy(fs.function_set_name, blk->tfb_tcp_block_name);
		fs.pcbcnt = blk->tfb_refcnt;
	}
	rw_runlock(&tcp_function_lock);	
	error = sysctl_handle_string(oidp, fs.function_set_name,
				     sizeof(fs.function_set_name), req);

	/* Check for error or no change */
	if (error != 0 || req->newptr == NULL)
		return(error);

	rw_wlock(&tcp_function_lock);
	blk = find_tcp_functions_locked(&fs);
	if ((blk == NULL) ||
	    (blk->tfb_flags & TCP_FUNC_BEING_REMOVED)) { 
		error = ENOENT; 
		goto done;
	}
	tcp_func_set_ptr = blk;
done:
	rw_wunlock(&tcp_function_lock);
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, OID_AUTO, functions_default,
	    CTLTYPE_STRING | CTLFLAG_RW,
	    NULL, 0, sysctl_net_inet_default_tcp_functions, "A",
	    "Set/get the default TCP functions");

static int
sysctl_net_inet_list_available(SYSCTL_HANDLER_ARGS)
{
	int error, cnt, linesz;
	struct tcp_function *f;
	char *buffer, *cp;
	size_t bufsz, outsz;
	bool alias;

	cnt = 0;
	rw_rlock(&tcp_function_lock);
	TAILQ_FOREACH(f, &t_functions, tf_next) {
		cnt++;
	}
	rw_runlock(&tcp_function_lock);

	bufsz = (cnt+2) * ((TCP_FUNCTION_NAME_LEN_MAX * 2) + 13) + 1;
	buffer = malloc(bufsz, M_TEMP, M_WAITOK);

	error = 0;
	cp = buffer;

	linesz = snprintf(cp, bufsz, "\n%-32s%c %-32s %s\n", "Stack", 'D',
	    "Alias", "PCB count");
	cp += linesz;
	bufsz -= linesz;
	outsz = linesz;

	rw_rlock(&tcp_function_lock);	
	TAILQ_FOREACH(f, &t_functions, tf_next) {
		alias = (f->tf_name != f->tf_fb->tfb_tcp_block_name);
		linesz = snprintf(cp, bufsz, "%-32s%c %-32s %u\n",
		    f->tf_fb->tfb_tcp_block_name,
		    (f->tf_fb == tcp_func_set_ptr) ? '*' : ' ',
		    alias ? f->tf_name : "-",
		    f->tf_fb->tfb_refcnt);
		if (linesz >= bufsz) {
			error = EOVERFLOW;
			break;
		}
		cp += linesz;
		bufsz -= linesz;
		outsz += linesz;
	}
	rw_runlock(&tcp_function_lock);
	if (error == 0)
		error = sysctl_handle_string(oidp, buffer, outsz + 1, req);
	free(buffer, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, OID_AUTO, functions_available,
	    CTLTYPE_STRING|CTLFLAG_RD,
	    NULL, 0, sysctl_net_inet_list_available, "A",
	    "list available TCP Function sets");

/*
 * Exports one (struct tcp_function_info) for each alias/name.
 */
static int
sysctl_net_inet_list_func_info(SYSCTL_HANDLER_ARGS)
{
	int cnt, error;
	struct tcp_function *f;
	struct tcp_function_info tfi;

	/*
	 * We don't allow writes.
	 */
	if (req->newptr != NULL)
		return (EINVAL);

	/*
	 * Wire the old buffer so we can directly copy the functions to
	 * user space without dropping the lock.
	 */
	if (req->oldptr != NULL) {
		error = sysctl_wire_old_buffer(req, 0);
		if (error)
			return (error);
	}

	/*
	 * Walk the list and copy out matching entries. If INVARIANTS
	 * is compiled in, also walk the list to verify the length of
	 * the list matches what we have recorded.
	 */
	rw_rlock(&tcp_function_lock);

	cnt = 0;
#ifndef INVARIANTS
	if (req->oldptr == NULL) {
		cnt = tcp_fb_cnt;
		goto skip_loop;
	}
#endif
	TAILQ_FOREACH(f, &t_functions, tf_next) {
#ifdef INVARIANTS
		cnt++;
#endif
		if (req->oldptr != NULL) {
			bzero(&tfi, sizeof(tfi));
			tfi.tfi_refcnt = f->tf_fb->tfb_refcnt;
			tfi.tfi_id = f->tf_fb->tfb_id;
			(void)strlcpy(tfi.tfi_alias, f->tf_name,
			    sizeof(tfi.tfi_alias));
			(void)strlcpy(tfi.tfi_name,
			    f->tf_fb->tfb_tcp_block_name, sizeof(tfi.tfi_name));
			error = SYSCTL_OUT(req, &tfi, sizeof(tfi));
			/*
			 * Don't stop on error, as that is the
			 * mechanism we use to accumulate length
			 * information if the buffer was too short.
			 */
		}
	}
	KASSERT(cnt == tcp_fb_cnt,
	    ("%s: cnt (%d) != tcp_fb_cnt (%d)", __func__, cnt, tcp_fb_cnt));
#ifndef INVARIANTS
skip_loop:
#endif
	rw_runlock(&tcp_function_lock);
	if (req->oldptr == NULL)
		error = SYSCTL_OUT(req, NULL,
		    (cnt + 1) * sizeof(struct tcp_function_info));

	return (error);
}

SYSCTL_PROC(_net_inet_tcp, OID_AUTO, function_info,
	    CTLTYPE_OPAQUE | CTLFLAG_SKIP | CTLFLAG_RD | CTLFLAG_MPSAFE,
	    NULL, 0, sysctl_net_inet_list_func_info, "S,tcp_function_info",
	    "List TCP function block name-to-ID mappings");

/*
 * tfb_tcp_handoff_ok() function for the default stack.
 * Note that we'll basically try to take all comers.
 */
static int
tcp_default_handoff_ok(struct tcpcb *tp)
{

	return (0);
}

/*
 * tfb_tcp_fb_init() function for the default stack.
 *
 * This handles making sure we have appropriate timers set if you are
 * transitioning a socket that has some amount of setup done.
 *
 * The init() fuction from the default can *never* return non-zero i.e.
 * it is required to always succeed since it is the stack of last resort!
 */
static int
tcp_default_fb_init(struct tcpcb *tp)
{

	struct socket *so;

	INP_WLOCK_ASSERT(tp->t_inpcb);

	KASSERT(tp->t_state >= 0 && tp->t_state < TCPS_TIME_WAIT,
	    ("%s: connection %p in unexpected state %d", __func__, tp,
	    tp->t_state));

	/*
	 * Nothing to do for ESTABLISHED or LISTEN states. And, we don't
	 * know what to do for unexpected states (which includes TIME_WAIT).
	 */
	if (tp->t_state <= TCPS_LISTEN || tp->t_state >= TCPS_TIME_WAIT)
		return (0);

	/*
	 * Make sure some kind of transmission timer is set if there is
	 * outstanding data.
	 */
	so = tp->t_inpcb->inp_socket;
	if ((!TCPS_HAVEESTABLISHED(tp->t_state) || sbavail(&so->so_snd) ||
	    tp->snd_una != tp->snd_max) && !(tcp_timer_active(tp, TT_REXMT) ||
	    tcp_timer_active(tp, TT_PERSIST))) {
		/*
		 * If the session has established and it looks like it should
		 * be in the persist state, set the persist timer. Otherwise,
		 * set the retransmit timer.
		 */
		if (TCPS_HAVEESTABLISHED(tp->t_state) && tp->snd_wnd == 0 &&
		    (int32_t)(tp->snd_nxt - tp->snd_una) <
		    (int32_t)sbavail(&so->so_snd))
			tcp_setpersist(tp);
		else
			tcp_timer_activate(tp, TT_REXMT, tp->t_rxtcur);
	}

	/* All non-embryonic sessions get a keepalive timer. */
	if (!tcp_timer_active(tp, TT_KEEP))
		tcp_timer_activate(tp, TT_KEEP,
		    TCPS_HAVEESTABLISHED(tp->t_state) ? TP_KEEPIDLE(tp) :
		    TP_KEEPINIT(tp));

	return (0);
}

/*
 * tfb_tcp_fb_fini() function for the default stack.
 *
 * This changes state as necessary (or prudent) to prepare for another stack
 * to assume responsibility for the connection.
 */
static void
tcp_default_fb_fini(struct tcpcb *tp, int tcb_is_purged)
{

	INP_WLOCK_ASSERT(tp->t_inpcb);
	return;
}

/*
 * Target size of TCP PCB hash tables. Must be a power of two.
 *
 * Note that this can be overridden by the kernel environment
 * variable net.inet.tcp.tcbhashsize
 */
#ifndef TCBHASHSIZE
#define TCBHASHSIZE	0
#endif

/*
 * XXX
 * Callouts should be moved into struct tcp directly.  They are currently
 * separate because the tcpcb structure is exported to userland for sysctl
 * parsing purposes, which do not know about callouts.
 */
struct tcpcb_mem {
	struct	tcpcb		tcb;
	struct	tcp_timer	tt;
	struct	cc_var		ccv;
#ifdef TCP_HHOOK
	struct	osd		osd;
#endif
};

VNET_DEFINE_STATIC(uma_zone_t, tcpcb_zone);
#define	V_tcpcb_zone			VNET(tcpcb_zone)

MALLOC_DEFINE(M_TCPLOG, "tcplog", "TCP address and flags print buffers");
MALLOC_DEFINE(M_TCPFUNCTIONS, "tcpfunc", "TCP function set memory");

static struct mtx isn_mtx;

#define	ISN_LOCK_INIT()	mtx_init(&isn_mtx, "isn_mtx", NULL, MTX_DEF)
#define	ISN_LOCK()	mtx_lock(&isn_mtx)
#define	ISN_UNLOCK()	mtx_unlock(&isn_mtx)

/*
 * TCP initialization.
 */
static void
tcp_zone_change(void *tag)
{

	uma_zone_set_max(V_tcbinfo.ipi_zone, maxsockets);
	uma_zone_set_max(V_tcpcb_zone, maxsockets);
	tcp_tw_zone_change();
}

static int
tcp_inpcb_init(void *mem, int size, int flags)
{
	struct inpcb *inp = mem;

	INP_LOCK_INIT(inp, "inp", "tcpinp");
	return (0);
}

/*
 * Take a value and get the next power of 2 that doesn't overflow.
 * Used to size the tcp_inpcb hash buckets.
 */
static int
maketcp_hashsize(int size)
{
	int hashsize;

	/*
	 * auto tune.
	 * get the next power of 2 higher than maxsockets.
	 */
	hashsize = 1 << fls(size);
	/* catch overflow, and just go one power of 2 smaller */
	if (hashsize < size) {
		hashsize = 1 << (fls(size) - 1);
	}
	return (hashsize);
}

static volatile int next_tcp_stack_id = 1;

/*
 * Register a TCP function block with the name provided in the names
 * array.  (Note that this function does NOT automatically register
 * blk->tfb_tcp_block_name as a stack name.  Therefore, you should
 * explicitly include blk->tfb_tcp_block_name in the list of names if
 * you wish to register the stack with that name.)
 *
 * Either all name registrations will succeed or all will fail.  If
 * a name registration fails, the function will update the num_names
 * argument to point to the array index of the name that encountered
 * the failure.
 *
 * Returns 0 on success, or an error code on failure.
 */
int
register_tcp_functions_as_names(struct tcp_function_block *blk, int wait,
    const char *names[], int *num_names)
{
	struct tcp_function *n;
	struct tcp_function_set fs;
	int error, i;

	KASSERT(names != NULL && *num_names > 0,
	    ("%s: Called with 0-length name list", __func__));
	KASSERT(names != NULL, ("%s: Called with NULL name list", __func__));
	KASSERT(rw_initialized(&tcp_function_lock),
	    ("%s: called too early", __func__));

	if ((blk->tfb_tcp_output == NULL) ||
	    (blk->tfb_tcp_do_segment == NULL) ||
	    (blk->tfb_tcp_ctloutput == NULL) ||
	    (strlen(blk->tfb_tcp_block_name) == 0)) {
		/* 
		 * These functions are required and you
		 * need a name.
		 */
		*num_names = 0;
		return (EINVAL);
	}
	if (blk->tfb_tcp_timer_stop_all ||
	    blk->tfb_tcp_timer_activate ||
	    blk->tfb_tcp_timer_active ||
	    blk->tfb_tcp_timer_stop) {
		/*
		 * If you define one timer function you 
		 * must have them all.
		 */
		if ((blk->tfb_tcp_timer_stop_all == NULL) ||
		    (blk->tfb_tcp_timer_activate == NULL) ||
		    (blk->tfb_tcp_timer_active == NULL) ||
		    (blk->tfb_tcp_timer_stop == NULL)) {
			*num_names = 0;
			return (EINVAL);
		}
	}

	refcount_init(&blk->tfb_refcnt, 0);
	blk->tfb_flags = 0;
	blk->tfb_id = atomic_fetchadd_int(&next_tcp_stack_id, 1);
	for (i = 0; i < *num_names; i++) {
		n = malloc(sizeof(struct tcp_function), M_TCPFUNCTIONS, wait);
		if (n == NULL) {
			error = ENOMEM;
			goto cleanup;
		}
		n->tf_fb = blk;

		(void)strlcpy(fs.function_set_name, names[i],
		    sizeof(fs.function_set_name));
		rw_wlock(&tcp_function_lock);
		if (find_tcp_functions_locked(&fs) != NULL) {
			/* Duplicate name space not allowed */
			rw_wunlock(&tcp_function_lock);
			free(n, M_TCPFUNCTIONS);
			error = EALREADY;
			goto cleanup;
		}
		(void)strlcpy(n->tf_name, names[i], sizeof(n->tf_name));
		TAILQ_INSERT_TAIL(&t_functions, n, tf_next);
		tcp_fb_cnt++;
		rw_wunlock(&tcp_function_lock);
	}
	return(0);

cleanup:
	/*
	 * Deregister the names we just added. Because registration failed
	 * for names[i], we don't need to deregister that name.
	 */
	*num_names = i;
	rw_wlock(&tcp_function_lock);
	while (--i >= 0) {
		TAILQ_FOREACH(n, &t_functions, tf_next) {
			if (!strncmp(n->tf_name, names[i],
			    TCP_FUNCTION_NAME_LEN_MAX)) {
				TAILQ_REMOVE(&t_functions, n, tf_next);
				tcp_fb_cnt--;
				n->tf_fb = NULL;
				free(n, M_TCPFUNCTIONS);
				break;
			}
		}
	}
	rw_wunlock(&tcp_function_lock);
	return (error);
}

/*
 * Register a TCP function block using the name provided in the name
 * argument.
 *
 * Returns 0 on success, or an error code on failure.
 */
int
register_tcp_functions_as_name(struct tcp_function_block *blk, const char *name,
    int wait)
{
	const char *name_list[1];
	int num_names, rv;

	num_names = 1;
	if (name != NULL)
		name_list[0] = name;
	else
		name_list[0] = blk->tfb_tcp_block_name;
	rv = register_tcp_functions_as_names(blk, wait, name_list, &num_names);
	return (rv);
}

/*
 * Register a TCP function block using the name defined in
 * blk->tfb_tcp_block_name.
 *
 * Returns 0 on success, or an error code on failure.
 */
int
register_tcp_functions(struct tcp_function_block *blk, int wait)
{

	return (register_tcp_functions_as_name(blk, NULL, wait));
}

/*
 * Deregister all names associated with a function block. This
 * functionally removes the function block from use within the system.
 *
 * When called with a true quiesce argument, mark the function block
 * as being removed so no more stacks will use it and determine
 * whether the removal would succeed.
 *
 * When called with a false quiesce argument, actually attempt the
 * removal.
 *
 * When called with a force argument, attempt to switch all TCBs to
 * use the default stack instead of returning EBUSY.
 *
 * Returns 0 on success (or if the removal would succeed, or an error
 * code on failure.
 */
int
deregister_tcp_functions(struct tcp_function_block *blk, bool quiesce,
    bool force)
{
	struct tcp_function *f;

	if (blk == &tcp_def_funcblk) {
		/* You can't un-register the default */
		return (EPERM);
	}
	rw_wlock(&tcp_function_lock);
	if (blk == tcp_func_set_ptr) {
		/* You can't free the current default */
		rw_wunlock(&tcp_function_lock);
		return (EBUSY);
	}
	/* Mark the block so no more stacks can use it. */
	blk->tfb_flags |= TCP_FUNC_BEING_REMOVED;
	/*
	 * If TCBs are still attached to the stack, attempt to switch them
	 * to the default stack.
	 */
	if (force && blk->tfb_refcnt) {
		struct inpcb *inp;
		struct tcpcb *tp;
		VNET_ITERATOR_DECL(vnet_iter);

		rw_wunlock(&tcp_function_lock);

		VNET_LIST_RLOCK();
		VNET_FOREACH(vnet_iter) {
			CURVNET_SET(vnet_iter);
			INP_INFO_WLOCK(&V_tcbinfo);
			CK_LIST_FOREACH(inp, V_tcbinfo.ipi_listhead, inp_list) {
				INP_WLOCK(inp);
				if (inp->inp_flags & INP_TIMEWAIT) {
					INP_WUNLOCK(inp);
					continue;
				}
				tp = intotcpcb(inp);
				if (tp == NULL || tp->t_fb != blk) {
					INP_WUNLOCK(inp);
					continue;
				}
				tcp_switch_back_to_default(tp);
				INP_WUNLOCK(inp);
			}
			INP_INFO_WUNLOCK(&V_tcbinfo);
			CURVNET_RESTORE();
		}
		VNET_LIST_RUNLOCK();

		rw_wlock(&tcp_function_lock);
	}
	if (blk->tfb_refcnt) {
		/* TCBs still attached. */
		rw_wunlock(&tcp_function_lock);
		return (EBUSY);
	}
	if (quiesce) {
		/* Skip removal. */
		rw_wunlock(&tcp_function_lock);
		return (0);
	}
	/* Remove any function names that map to this function block. */
	while (find_tcp_fb_locked(blk, &f) != NULL) {
		TAILQ_REMOVE(&t_functions, f, tf_next);
		tcp_fb_cnt--;
		f->tf_fb = NULL;
		free(f, M_TCPFUNCTIONS);
	}
	rw_wunlock(&tcp_function_lock);
	return (0);
}

void
tcp_init(void)
{
	const char *tcbhash_tuneable;
	int hashsize;

	tcbhash_tuneable = "net.inet.tcp.tcbhashsize";

#ifdef TCP_HHOOK
	if (hhook_head_register(HHOOK_TYPE_TCP, HHOOK_TCP_EST_IN,
	    &V_tcp_hhh[HHOOK_TCP_EST_IN], HHOOK_NOWAIT|HHOOK_HEADISINVNET) != 0)
		printf("%s: WARNING: unable to register helper hook\n", __func__);
	if (hhook_head_register(HHOOK_TYPE_TCP, HHOOK_TCP_EST_OUT,
	    &V_tcp_hhh[HHOOK_TCP_EST_OUT], HHOOK_NOWAIT|HHOOK_HEADISINVNET) != 0)
		printf("%s: WARNING: unable to register helper hook\n", __func__);
#endif
	hashsize = TCBHASHSIZE;
	TUNABLE_INT_FETCH(tcbhash_tuneable, &hashsize);
	if (hashsize == 0) {
		/*
		 * Auto tune the hash size based on maxsockets.
		 * A perfect hash would have a 1:1 mapping
		 * (hashsize = maxsockets) however it's been
		 * suggested that O(2) average is better.
		 */
		hashsize = maketcp_hashsize(maxsockets / 4);
		/*
		 * Our historical default is 512,
		 * do not autotune lower than this.
		 */
		if (hashsize < 512)
			hashsize = 512;
		if (bootverbose && IS_DEFAULT_VNET(curvnet))
			printf("%s: %s auto tuned to %d\n", __func__,
			    tcbhash_tuneable, hashsize);
	}
	/*
	 * We require a hashsize to be a power of two.
	 * Previously if it was not a power of two we would just reset it
	 * back to 512, which could be a nasty surprise if you did not notice
	 * the error message.
	 * Instead what we do is clip it to the closest power of two lower
	 * than the specified hash value.
	 */
	if (!powerof2(hashsize)) {
		int oldhashsize = hashsize;

		hashsize = maketcp_hashsize(hashsize);
		/* prevent absurdly low value */
		if (hashsize < 16)
			hashsize = 16;
		printf("%s: WARNING: TCB hash size not a power of 2, "
		    "clipped from %d to %d.\n", __func__, oldhashsize,
		    hashsize);
	}
	in_pcbinfo_init(&V_tcbinfo, "tcp", &V_tcb, hashsize, hashsize,
	    "tcp_inpcb", tcp_inpcb_init, IPI_HASHFIELDS_4TUPLE);

	/*
	 * These have to be type stable for the benefit of the timers.
	 */
	V_tcpcb_zone = uma_zcreate("tcpcb", sizeof(struct tcpcb_mem),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	uma_zone_set_max(V_tcpcb_zone, maxsockets);
	uma_zone_set_warning(V_tcpcb_zone, "kern.ipc.maxsockets limit reached");

	tcp_tw_init();
	syncache_init();
	tcp_hc_init();

	TUNABLE_INT_FETCH("net.inet.tcp.sack.enable", &V_tcp_do_sack);
	V_sack_hole_zone = uma_zcreate("sackhole", sizeof(struct sackhole),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);

	tcp_fastopen_init();

	/* Skip initialization of globals for non-default instances. */
	if (!IS_DEFAULT_VNET(curvnet))
		return;

	tcp_reass_global_init();

	/* XXX virtualize those bellow? */
	tcp_delacktime = TCPTV_DELACK;
	tcp_keepinit = TCPTV_KEEP_INIT;
	tcp_keepidle = TCPTV_KEEP_IDLE;
	tcp_keepintvl = TCPTV_KEEPINTVL;
	tcp_maxpersistidle = TCPTV_KEEP_IDLE;
	tcp_msl = TCPTV_MSL;
	tcp_rexmit_initial = TCPTV_RTOBASE;
	if (tcp_rexmit_initial < 1)
		tcp_rexmit_initial = 1;
	tcp_rexmit_min = TCPTV_MIN;
	if (tcp_rexmit_min < 1)
		tcp_rexmit_min = 1;
	tcp_persmin = TCPTV_PERSMIN;
	tcp_persmax = TCPTV_PERSMAX;
	tcp_rexmit_slop = TCPTV_CPU_VAR;
	tcp_finwait2_timeout = TCPTV_FINWAIT2_TIMEOUT;
	tcp_tcbhashsize = hashsize;

	/* Setup the tcp function block list */
	TAILQ_INIT(&t_functions);
	rw_init(&tcp_function_lock, "tcp_func_lock");
	register_tcp_functions(&tcp_def_funcblk, M_WAITOK);
#ifdef TCP_BLACKBOX
	/* Initialize the TCP logging data. */
	tcp_log_init();
#endif
	arc4rand(&V_ts_offset_secret, sizeof(V_ts_offset_secret), 0);

	if (tcp_soreceive_stream) {
#ifdef INET
		tcp_usrreqs.pru_soreceive = soreceive_stream;
#endif
#ifdef INET6
		tcp6_usrreqs.pru_soreceive = soreceive_stream;
#endif /* INET6 */
	}

#ifdef INET6
#define TCP_MINPROTOHDR (sizeof(struct ip6_hdr) + sizeof(struct tcphdr))
#else /* INET6 */
#define TCP_MINPROTOHDR (sizeof(struct tcpiphdr))
#endif /* INET6 */
	if (max_protohdr < TCP_MINPROTOHDR)
		max_protohdr = TCP_MINPROTOHDR;
	if (max_linkhdr + TCP_MINPROTOHDR > MHLEN)
		panic("tcp_init");
#undef TCP_MINPROTOHDR

	ISN_LOCK_INIT();
	EVENTHANDLER_REGISTER(shutdown_pre_sync, tcp_fini, NULL,
		SHUTDOWN_PRI_DEFAULT);
	EVENTHANDLER_REGISTER(maxsockets_change, tcp_zone_change, NULL,
		EVENTHANDLER_PRI_ANY);
#ifdef TCPPCAP
	tcp_pcap_init();
#endif
}

#ifdef VIMAGE
static void
tcp_destroy(void *unused __unused)
{
	int n;
#ifdef TCP_HHOOK
	int error;
#endif

	/*
	 * All our processes are gone, all our sockets should be cleaned
	 * up, which means, we should be past the tcp_discardcb() calls.
	 * Sleep to let all tcpcb timers really disappear and cleanup.
	 */
	for (;;) {
		INP_LIST_RLOCK(&V_tcbinfo);
		n = V_tcbinfo.ipi_count;
		INP_LIST_RUNLOCK(&V_tcbinfo);
		if (n == 0)
			break;
		pause("tcpdes", hz / 10);
	}
	tcp_hc_destroy();
	syncache_destroy();
	tcp_tw_destroy();
	in_pcbinfo_destroy(&V_tcbinfo);
	/* tcp_discardcb() clears the sack_holes up. */
	uma_zdestroy(V_sack_hole_zone);
	uma_zdestroy(V_tcpcb_zone);

	/*
	 * Cannot free the zone until all tcpcbs are released as we attach
	 * the allocations to them.
	 */
	tcp_fastopen_destroy();

#ifdef TCP_HHOOK
	error = hhook_head_deregister(V_tcp_hhh[HHOOK_TCP_EST_IN]);
	if (error != 0) {
		printf("%s: WARNING: unable to deregister helper hook "
		    "type=%d, id=%d: error %d returned\n", __func__,
		    HHOOK_TYPE_TCP, HHOOK_TCP_EST_IN, error);
	}
	error = hhook_head_deregister(V_tcp_hhh[HHOOK_TCP_EST_OUT]);
	if (error != 0) {
		printf("%s: WARNING: unable to deregister helper hook "
		    "type=%d, id=%d: error %d returned\n", __func__,
		    HHOOK_TYPE_TCP, HHOOK_TCP_EST_OUT, error);
	}
#endif
}
VNET_SYSUNINIT(tcp, SI_SUB_PROTO_DOMAIN, SI_ORDER_FOURTH, tcp_destroy, NULL);
#endif

void
tcp_fini(void *xtp)
{

}

/*
 * Fill in the IP and TCP headers for an outgoing packet, given the tcpcb.
 * tcp_template used to store this data in mbufs, but we now recopy it out
 * of the tcpcb each time to conserve mbufs.
 */
void
tcpip_fillheaders(struct inpcb *inp, void *ip_ptr, void *tcp_ptr)
{
	struct tcphdr *th = (struct tcphdr *)tcp_ptr;

	INP_WLOCK_ASSERT(inp);

#ifdef INET6
	if ((inp->inp_vflag & INP_IPV6) != 0) {
		struct ip6_hdr *ip6;

		ip6 = (struct ip6_hdr *)ip_ptr;
		ip6->ip6_flow = (ip6->ip6_flow & ~IPV6_FLOWINFO_MASK) |
			(inp->inp_flow & IPV6_FLOWINFO_MASK);
		ip6->ip6_vfc = (ip6->ip6_vfc & ~IPV6_VERSION_MASK) |
			(IPV6_VERSION & IPV6_VERSION_MASK);
		ip6->ip6_nxt = IPPROTO_TCP;
		ip6->ip6_plen = htons(sizeof(struct tcphdr));
		ip6->ip6_src = inp->in6p_laddr;
		ip6->ip6_dst = inp->in6p_faddr;
	}
#endif /* INET6 */
#if defined(INET6) && defined(INET)
	else
#endif
#ifdef INET
	{
		struct ip *ip;

		ip = (struct ip *)ip_ptr;
		ip->ip_v = IPVERSION;
		ip->ip_hl = 5;
		ip->ip_tos = inp->inp_ip_tos;
		ip->ip_len = 0;
		ip->ip_id = 0;
		ip->ip_off = 0;
		ip->ip_ttl = inp->inp_ip_ttl;
		ip->ip_sum = 0;
		ip->ip_p = IPPROTO_TCP;
		ip->ip_src = inp->inp_laddr;
		ip->ip_dst = inp->inp_faddr;
	}
#endif /* INET */
	th->th_sport = inp->inp_lport;
	th->th_dport = inp->inp_fport;
	th->th_seq = 0;
	th->th_ack = 0;
	th->th_x2 = 0;
	th->th_off = 5;
	th->th_flags = 0;
	th->th_win = 0;
	th->th_urp = 0;
	th->th_sum = 0;		/* in_pseudo() is called later for ipv4 */
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Allocates an mbuf and fills in a skeletal tcp/ip header.  The only
 * use for this function is in keepalives, which use tcp_respond.
 */
struct tcptemp *
tcpip_maketemplate(struct inpcb *inp)
{
	struct tcptemp *t;

	t = malloc(sizeof(*t), M_TEMP, M_NOWAIT);
	if (t == NULL)
		return (NULL);
	tcpip_fillheaders(inp, (void *)&t->tt_ipgen, (void *)&t->tt_t);
	return (t);
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == NULL, then we make a copy
 * of the tcpiphdr at th and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection.  If flags are given then we send
 * a message back to the TCP which originated the segment th,
 * and discard the mbuf containing it and any other attached mbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 *
 * NOTE: If m != NULL, then th must point to *inside* the mbuf.
 */
void
tcp_respond(struct tcpcb *tp, void *ipgen, struct tcphdr *th, struct mbuf *m,
    tcp_seq ack, tcp_seq seq, int flags)
{
	struct tcpopt to;
	struct inpcb *inp;
	struct ip *ip;
	struct mbuf *optm;
	struct tcphdr *nth;
	u_char *optp;
#ifdef INET6
	struct ip6_hdr *ip6;
	int isipv6;
#endif /* INET6 */
	int optlen, tlen, win;
	bool incl_opts;

	KASSERT(tp != NULL || m != NULL, ("tcp_respond: tp and m both NULL"));

#ifdef INET6
	isipv6 = ((struct ip *)ipgen)->ip_v == (IPV6_VERSION >> 4);
	ip6 = ipgen;
#endif /* INET6 */
	ip = ipgen;

	if (tp != NULL) {
		inp = tp->t_inpcb;
		KASSERT(inp != NULL, ("tcp control block w/o inpcb"));
		INP_WLOCK_ASSERT(inp);
	} else
		inp = NULL;

	incl_opts = false;
	win = 0;
	if (tp != NULL) {
		if (!(flags & TH_RST)) {
			win = sbspace(&inp->inp_socket->so_rcv);
			if (win > TCP_MAXWIN << tp->rcv_scale)
				win = TCP_MAXWIN << tp->rcv_scale;
		}
		if ((tp->t_flags & TF_NOOPT) == 0)
			incl_opts = true;
	}
	if (m == NULL) {
		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL)
			return;
		m->m_data += max_linkhdr;
#ifdef INET6
		if (isipv6) {
			bcopy((caddr_t)ip6, mtod(m, caddr_t),
			      sizeof(struct ip6_hdr));
			ip6 = mtod(m, struct ip6_hdr *);
			nth = (struct tcphdr *)(ip6 + 1);
		} else
#endif /* INET6 */
		{
			bcopy((caddr_t)ip, mtod(m, caddr_t), sizeof(struct ip));
			ip = mtod(m, struct ip *);
			nth = (struct tcphdr *)(ip + 1);
		}
		bcopy((caddr_t)th, (caddr_t)nth, sizeof(struct tcphdr));
		flags = TH_ACK;
	} else if (!M_WRITABLE(m)) {
		struct mbuf *n;

		/* Can't reuse 'm', allocate a new mbuf. */
		n = m_gethdr(M_NOWAIT, MT_DATA);
		if (n == NULL) {
			m_freem(m);
			return;
		}

		if (!m_dup_pkthdr(n, m, M_NOWAIT)) {
			m_freem(m);
			m_freem(n);
			return;
		}

		n->m_data += max_linkhdr;
		/* m_len is set later */
#define xchg(a,b,type) { type t; t=a; a=b; b=t; }
#ifdef INET6
		if (isipv6) {
			bcopy((caddr_t)ip6, mtod(n, caddr_t),
			      sizeof(struct ip6_hdr));
			ip6 = mtod(n, struct ip6_hdr *);
			xchg(ip6->ip6_dst, ip6->ip6_src, struct in6_addr);
			nth = (struct tcphdr *)(ip6 + 1);
		} else
#endif /* INET6 */
		{
			bcopy((caddr_t)ip, mtod(n, caddr_t), sizeof(struct ip));
			ip = mtod(n, struct ip *);
			xchg(ip->ip_dst.s_addr, ip->ip_src.s_addr, uint32_t);
			nth = (struct tcphdr *)(ip + 1);
		}
		bcopy((caddr_t)th, (caddr_t)nth, sizeof(struct tcphdr));
		xchg(nth->th_dport, nth->th_sport, uint16_t);
		th = nth;
		m_freem(m);
		m = n;
	} else {
		/*
		 *  reuse the mbuf. 
		 * XXX MRT We inherit the FIB, which is lucky.
		 */
		m_freem(m->m_next);
		m->m_next = NULL;
		m->m_data = (caddr_t)ipgen;
		/* m_len is set later */
#ifdef INET6
		if (isipv6) {
			xchg(ip6->ip6_dst, ip6->ip6_src, struct in6_addr);
			nth = (struct tcphdr *)(ip6 + 1);
		} else
#endif /* INET6 */
		{
			xchg(ip->ip_dst.s_addr, ip->ip_src.s_addr, uint32_t);
			nth = (struct tcphdr *)(ip + 1);
		}
		if (th != nth) {
			/*
			 * this is usually a case when an extension header
			 * exists between the IPv6 header and the
			 * TCP header.
			 */
			nth->th_sport = th->th_sport;
			nth->th_dport = th->th_dport;
		}
		xchg(nth->th_dport, nth->th_sport, uint16_t);
#undef xchg
	}
	tlen = 0;
#ifdef INET6
	if (isipv6)
		tlen = sizeof (struct ip6_hdr) + sizeof (struct tcphdr);
#endif
#if defined(INET) && defined(INET6)
	else
#endif
#ifdef INET
		tlen = sizeof (struct tcpiphdr);
#endif
#ifdef INVARIANTS
	m->m_len = 0;
	KASSERT(M_TRAILINGSPACE(m) >= tlen,
	    ("Not enough trailing space for message (m=%p, need=%d, have=%ld)",
	    m, tlen, (long)M_TRAILINGSPACE(m)));
#endif
	m->m_len = tlen;
	to.to_flags = 0;
	if (incl_opts) {
		/* Make sure we have room. */
		if (M_TRAILINGSPACE(m) < TCP_MAXOLEN) {
			m->m_next = m_get(M_NOWAIT, MT_DATA);
			if (m->m_next) {
				optp = mtod(m->m_next, u_char *);
				optm = m->m_next;
			} else
				incl_opts = false;
		} else {
			optp = (u_char *) (nth + 1);
			optm = m;
		}
	}
	if (incl_opts) {
		/* Timestamps. */
		if (tp->t_flags & TF_RCVD_TSTMP) {
			to.to_tsval = tcp_ts_getticks() + tp->ts_offset;
			to.to_tsecr = tp->ts_recent;
			to.to_flags |= TOF_TS;
		}
#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
		/* TCP-MD5 (RFC2385). */
		if (tp->t_flags & TF_SIGNATURE)
			to.to_flags |= TOF_SIGNATURE;
#endif
		/* Add the options. */
		tlen += optlen = tcp_addoptions(&to, optp);

		/* Update m_len in the correct mbuf. */
		optm->m_len += optlen;
	} else
		optlen = 0;
#ifdef INET6
	if (isipv6) {
		ip6->ip6_flow = 0;
		ip6->ip6_vfc = IPV6_VERSION;
		ip6->ip6_nxt = IPPROTO_TCP;
		ip6->ip6_plen = htons(tlen - sizeof(*ip6));
	}
#endif
#if defined(INET) && defined(INET6)
	else
#endif
#ifdef INET
	{
		ip->ip_len = htons(tlen);
		ip->ip_ttl = V_ip_defttl;
		if (V_path_mtu_discovery)
			ip->ip_off |= htons(IP_DF);
	}
#endif
	m->m_pkthdr.len = tlen;
	m->m_pkthdr.rcvif = NULL;
#ifdef MAC
	if (inp != NULL) {
		/*
		 * Packet is associated with a socket, so allow the
		 * label of the response to reflect the socket label.
		 */
		INP_WLOCK_ASSERT(inp);
		mac_inpcb_create_mbuf(inp, m);
	} else {
		/*
		 * Packet is not associated with a socket, so possibly
		 * update the label in place.
		 */
		mac_netinet_tcp_reply(m);
	}
#endif
	nth->th_seq = htonl(seq);
	nth->th_ack = htonl(ack);
	nth->th_x2 = 0;
	nth->th_off = (sizeof (struct tcphdr) + optlen) >> 2;
	nth->th_flags = flags;
	if (tp != NULL)
		nth->th_win = htons((u_short) (win >> tp->rcv_scale));
	else
		nth->th_win = htons((u_short)win);
	nth->th_urp = 0;

#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
	if (to.to_flags & TOF_SIGNATURE) {
		if (!TCPMD5_ENABLED() ||
		    TCPMD5_OUTPUT(m, nth, to.to_signature) != 0) {
			m_freem(m);
			return;
		}
	}
#endif

	m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
#ifdef INET6
	if (isipv6) {
		m->m_pkthdr.csum_flags = CSUM_TCP_IPV6;
		nth->th_sum = in6_cksum_pseudo(ip6,
		    tlen - sizeof(struct ip6_hdr), IPPROTO_TCP, 0);
		ip6->ip6_hlim = in6_selecthlim(tp != NULL ? tp->t_inpcb :
		    NULL, NULL);
	}
#endif /* INET6 */
#if defined(INET6) && defined(INET)
	else
#endif
#ifdef INET
	{
		m->m_pkthdr.csum_flags = CSUM_TCP;
		nth->th_sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    htons((u_short)(tlen - sizeof(struct ip) + ip->ip_p)));
	}
#endif /* INET */
#ifdef TCPDEBUG
	if (tp == NULL || (inp->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_OUTPUT, 0, tp, mtod(m, void *), th, 0);
#endif
	TCP_PROBE3(debug__output, tp, th, m);
	if (flags & TH_RST)
		TCP_PROBE5(accept__refused, NULL, NULL, m, tp, nth);

#ifdef INET6
	if (isipv6) {
		TCP_PROBE5(send, NULL, tp, ip6, tp, nth);
		(void)ip6_output(m, NULL, NULL, 0, NULL, NULL, inp);
	}
#endif /* INET6 */
#if defined(INET) && defined(INET6)
	else
#endif
#ifdef INET
	{
		TCP_PROBE5(send, NULL, tp, ip, tp, nth);
		(void)ip_output(m, NULL, NULL, 0, NULL, inp);
	}
#endif
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.  The `inp' parameter must have
 * come from the zone allocator set up in tcp_init().
 */
struct tcpcb *
tcp_newtcpcb(struct inpcb *inp)
{
	struct tcpcb_mem *tm;
	struct tcpcb *tp;
#ifdef INET6
	int isipv6 = (inp->inp_vflag & INP_IPV6) != 0;
#endif /* INET6 */

	tm = uma_zalloc(V_tcpcb_zone, M_NOWAIT | M_ZERO);
	if (tm == NULL)
		return (NULL);
	tp = &tm->tcb;

	/* Initialise cc_var struct for this tcpcb. */
	tp->ccv = &tm->ccv;
	tp->ccv->type = IPPROTO_TCP;
	tp->ccv->ccvc.tcp = tp;
	rw_rlock(&tcp_function_lock);
	tp->t_fb = tcp_func_set_ptr;
	refcount_acquire(&tp->t_fb->tfb_refcnt);
	rw_runlock(&tcp_function_lock);
	/*
	 * Use the current system default CC algorithm.
	 */
	CC_LIST_RLOCK();
	KASSERT(!STAILQ_EMPTY(&cc_list), ("cc_list is empty!"));
	CC_ALGO(tp) = CC_DEFAULT();
	CC_LIST_RUNLOCK();

	if (CC_ALGO(tp)->cb_init != NULL)
		if (CC_ALGO(tp)->cb_init(tp->ccv) > 0) {
			if (tp->t_fb->tfb_tcp_fb_fini)
				(*tp->t_fb->tfb_tcp_fb_fini)(tp, 1);
			refcount_release(&tp->t_fb->tfb_refcnt);
			uma_zfree(V_tcpcb_zone, tm);
			return (NULL);
		}

#ifdef TCP_HHOOK
	tp->osd = &tm->osd;
	if (khelp_init_osd(HELPER_CLASS_TCP, tp->osd)) {
		if (tp->t_fb->tfb_tcp_fb_fini)
			(*tp->t_fb->tfb_tcp_fb_fini)(tp, 1);
		refcount_release(&tp->t_fb->tfb_refcnt);
		uma_zfree(V_tcpcb_zone, tm);
		return (NULL);
	}
#endif

#ifdef VIMAGE
	tp->t_vnet = inp->inp_vnet;
#endif
	tp->t_timers = &tm->tt;
	TAILQ_INIT(&tp->t_segq);
	tp->t_maxseg =
#ifdef INET6
		isipv6 ? V_tcp_v6mssdflt :
#endif /* INET6 */
		V_tcp_mssdflt;

	/* Set up our timeouts. */
	callout_init(&tp->t_timers->tt_rexmt, 1);
	callout_init(&tp->t_timers->tt_persist, 1);
	callout_init(&tp->t_timers->tt_keep, 1);
	callout_init(&tp->t_timers->tt_2msl, 1);
	callout_init(&tp->t_timers->tt_delack, 1);

	if (V_tcp_do_rfc1323)
		tp->t_flags = (TF_REQ_SCALE|TF_REQ_TSTMP);
	if (V_tcp_do_sack)
		tp->t_flags |= TF_SACK_PERMIT;
	TAILQ_INIT(&tp->snd_holes);
	/*
	 * The tcpcb will hold a reference on its inpcb until tcp_discardcb()
	 * is called.
	 */
	in_pcbref(inp);	/* Reference for tcpcb */
	tp->t_inpcb = inp;

	/*
	 * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
	 * rtt estimate.  Set rttvar so that srtt + 4 * rttvar gives
	 * reasonable initial retransmit time.
	 */
	tp->t_srtt = TCPTV_SRTTBASE;
	tp->t_rttvar = ((tcp_rexmit_initial - TCPTV_SRTTBASE) << TCP_RTTVAR_SHIFT) / 4;
	tp->t_rttmin = tcp_rexmit_min;
	tp->t_rxtcur = tcp_rexmit_initial;
	tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->t_rcvtime = ticks;
	/*
	 * IPv4 TTL initialization is necessary for an IPv6 socket as well,
	 * because the socket may be bound to an IPv6 wildcard address,
	 * which may match an IPv4-mapped IPv6 address.
	 */
	inp->inp_ip_ttl = V_ip_defttl;
	inp->inp_ppcb = tp;
#ifdef TCPPCAP
	/*
	 * Init the TCP PCAP queues.
	 */
	tcp_pcap_tcpcb_init(tp);
#endif
#ifdef TCP_BLACKBOX
	/* Initialize the per-TCPCB log data. */
	tcp_log_tcpcbinit(tp);
#endif
	if (tp->t_fb->tfb_tcp_fb_init) {
		(*tp->t_fb->tfb_tcp_fb_init)(tp);
	}
	return (tp);		/* XXX */
}

/*
 * Switch the congestion control algorithm back to NewReno for any active
 * control blocks using an algorithm which is about to go away.
 * This ensures the CC framework can allow the unload to proceed without leaving
 * any dangling pointers which would trigger a panic.
 * Returning non-zero would inform the CC framework that something went wrong
 * and it would be unsafe to allow the unload to proceed. However, there is no
 * way for this to occur with this implementation so we always return zero.
 */
int
tcp_ccalgounload(struct cc_algo *unload_algo)
{
	struct cc_algo *tmpalgo;
	struct inpcb *inp;
	struct tcpcb *tp;
	VNET_ITERATOR_DECL(vnet_iter);

	/*
	 * Check all active control blocks across all network stacks and change
	 * any that are using "unload_algo" back to NewReno. If "unload_algo"
	 * requires cleanup code to be run, call it.
	 */
	VNET_LIST_RLOCK();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		INP_INFO_WLOCK(&V_tcbinfo);
		/*
		 * New connections already part way through being initialised
		 * with the CC algo we're removing will not race with this code
		 * because the INP_INFO_WLOCK is held during initialisation. We
		 * therefore don't enter the loop below until the connection
		 * list has stabilised.
		 */
		CK_LIST_FOREACH(inp, &V_tcb, inp_list) {
			INP_WLOCK(inp);
			/* Important to skip tcptw structs. */
			if (!(inp->inp_flags & INP_TIMEWAIT) &&
			    (tp = intotcpcb(inp)) != NULL) {
				/*
				 * By holding INP_WLOCK here, we are assured
				 * that the connection is not currently
				 * executing inside the CC module's functions
				 * i.e. it is safe to make the switch back to
				 * NewReno.
				 */
				if (CC_ALGO(tp) == unload_algo) {
					tmpalgo = CC_ALGO(tp);
					if (tmpalgo->cb_destroy != NULL)
						tmpalgo->cb_destroy(tp->ccv);
					CC_DATA(tp) = NULL;
					/*
					 * NewReno may allocate memory on
					 * demand for certain stateful
					 * configuration as needed, but is
					 * coded to never fail on memory
					 * allocation failure so it is a safe
					 * fallback.
					 */
					CC_ALGO(tp) = &newreno_cc_algo;
				}
			}
			INP_WUNLOCK(inp);
		}
		INP_INFO_WUNLOCK(&V_tcbinfo);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK();

	return (0);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *
tcp_drop(struct tcpcb *tp, int errno)
{
	struct socket *so = tp->t_inpcb->inp_socket;

	INP_INFO_LOCK_ASSERT(&V_tcbinfo);
	INP_WLOCK_ASSERT(tp->t_inpcb);

	if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tcp_state_change(tp, TCPS_CLOSED);
		(void) tp->t_fb->tfb_tcp_output(tp);
		TCPSTAT_INC(tcps_drops);
	} else
		TCPSTAT_INC(tcps_conndrops);
	if (errno == ETIMEDOUT && tp->t_softerror)
		errno = tp->t_softerror;
	so->so_error = errno;
	return (tcp_close(tp));
}

void
tcp_discardcb(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
#ifdef INET6
	int isipv6 = (inp->inp_vflag & INP_IPV6) != 0;
#endif /* INET6 */
	int released __unused;

	INP_WLOCK_ASSERT(inp);

	/*
	 * Make sure that all of our timers are stopped before we delete the
	 * PCB.
	 *
	 * If stopping a timer fails, we schedule a discard function in same
	 * callout, and the last discard function called will take care of
	 * deleting the tcpcb.
	 */
	tp->t_timers->tt_draincnt = 0;
	tcp_timer_stop(tp, TT_REXMT);
	tcp_timer_stop(tp, TT_PERSIST);
	tcp_timer_stop(tp, TT_KEEP);
	tcp_timer_stop(tp, TT_2MSL);
	tcp_timer_stop(tp, TT_DELACK);
	if (tp->t_fb->tfb_tcp_timer_stop_all) {
		/* 
		 * Call the stop-all function of the methods, 
		 * this function should call the tcp_timer_stop()
		 * method with each of the function specific timeouts.
		 * That stop will be called via the tfb_tcp_timer_stop()
		 * which should use the async drain function of the 
		 * callout system (see tcp_var.h).
		 */
		tp->t_fb->tfb_tcp_timer_stop_all(tp);
	}

	/*
	 * If we got enough samples through the srtt filter,
	 * save the rtt and rttvar in the routing entry.
	 * 'Enough' is arbitrarily defined as 4 rtt samples.
	 * 4 samples is enough for the srtt filter to converge
	 * to within enough % of the correct value; fewer samples
	 * and we could save a bogus rtt. The danger is not high
	 * as tcp quickly recovers from everything.
	 * XXX: Works very well but needs some more statistics!
	 */
	if (tp->t_rttupdated >= 4) {
		struct hc_metrics_lite metrics;
		uint32_t ssthresh;

		bzero(&metrics, sizeof(metrics));
		/*
		 * Update the ssthresh always when the conditions below
		 * are satisfied. This gives us better new start value
		 * for the congestion avoidance for new connections.
		 * ssthresh is only set if packet loss occurred on a session.
		 *
		 * XXXRW: 'so' may be NULL here, and/or socket buffer may be
		 * being torn down.  Ideally this code would not use 'so'.
		 */
		ssthresh = tp->snd_ssthresh;
		if (ssthresh != 0 && ssthresh < so->so_snd.sb_hiwat / 2) {
			/*
			 * convert the limit from user data bytes to
			 * packets then to packet data bytes.
			 */
			ssthresh = (ssthresh + tp->t_maxseg / 2) / tp->t_maxseg;
			if (ssthresh < 2)
				ssthresh = 2;
			ssthresh *= (tp->t_maxseg +
#ifdef INET6
			    (isipv6 ? sizeof (struct ip6_hdr) +
				sizeof (struct tcphdr) :
#endif
				sizeof (struct tcpiphdr)
#ifdef INET6
			    )
#endif
			    );
		} else
			ssthresh = 0;
		metrics.rmx_ssthresh = ssthresh;

		metrics.rmx_rtt = tp->t_srtt;
		metrics.rmx_rttvar = tp->t_rttvar;
		metrics.rmx_cwnd = tp->snd_cwnd;
		metrics.rmx_sendpipe = 0;
		metrics.rmx_recvpipe = 0;

		tcp_hc_update(&inp->inp_inc, &metrics);
	}

	/* free the reassembly queue, if any */
	tcp_reass_flush(tp);

#ifdef TCP_OFFLOAD
	/* Disconnect offload device, if any. */
	if (tp->t_flags & TF_TOE)
		tcp_offload_detach(tp);
#endif
		
	tcp_free_sackholes(tp);

#ifdef TCPPCAP
	/* Free the TCP PCAP queues. */
	tcp_pcap_drain(&(tp->t_inpkts));
	tcp_pcap_drain(&(tp->t_outpkts));
#endif

	/* Allow the CC algorithm to clean up after itself. */
	if (CC_ALGO(tp)->cb_destroy != NULL)
		CC_ALGO(tp)->cb_destroy(tp->ccv);
	CC_DATA(tp) = NULL;

#ifdef TCP_HHOOK
	khelp_destroy_osd(tp->osd);
#endif

	CC_ALGO(tp) = NULL;
	inp->inp_ppcb = NULL;
	if (tp->t_timers->tt_draincnt == 0) {
		/* We own the last reference on tcpcb, let's free it. */
#ifdef TCP_BLACKBOX
		tcp_log_tcpcbfini(tp);
#endif
		TCPSTATES_DEC(tp->t_state);
		if (tp->t_fb->tfb_tcp_fb_fini)
			(*tp->t_fb->tfb_tcp_fb_fini)(tp, 1);
		refcount_release(&tp->t_fb->tfb_refcnt);
		tp->t_inpcb = NULL;
		uma_zfree(V_tcpcb_zone, tp);
		released = in_pcbrele_wlocked(inp);
		KASSERT(!released, ("%s: inp %p should not have been released "
			"here", __func__, inp));
	}
}

void
tcp_timer_discard(void *ptp)
{
	struct inpcb *inp;
	struct tcpcb *tp;
	struct epoch_tracker et;
	
	tp = (struct tcpcb *)ptp;
	CURVNET_SET(tp->t_vnet);
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	inp = tp->t_inpcb;
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL",
		__func__, tp));
	INP_WLOCK(inp);
	KASSERT((tp->t_timers->tt_flags & TT_STOPPED) != 0,
		("%s: tcpcb has to be stopped here", __func__));
	tp->t_timers->tt_draincnt--;
	if (tp->t_timers->tt_draincnt == 0) {
		/* We own the last reference on this tcpcb, let's free it. */
#ifdef TCP_BLACKBOX
		tcp_log_tcpcbfini(tp);
#endif
		TCPSTATES_DEC(tp->t_state);
		if (tp->t_fb->tfb_tcp_fb_fini)
			(*tp->t_fb->tfb_tcp_fb_fini)(tp, 1);
		refcount_release(&tp->t_fb->tfb_refcnt);
		tp->t_inpcb = NULL;
		uma_zfree(V_tcpcb_zone, tp);
		if (in_pcbrele_wlocked(inp)) {
			INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
			CURVNET_RESTORE();
			return;
		}
	}
	INP_WUNLOCK(inp);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	CURVNET_RESTORE();
}

/*
 * Attempt to close a TCP control block, marking it as dropped, and freeing
 * the socket if we hold the only reference.
 */
struct tcpcb *
tcp_close(struct tcpcb *tp)
{
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so;

	INP_INFO_LOCK_ASSERT(&V_tcbinfo);
	INP_WLOCK_ASSERT(inp);

#ifdef TCP_OFFLOAD
	if (tp->t_state == TCPS_LISTEN)
		tcp_offload_listen_stop(tp);
#endif
	/*
	 * This releases the TFO pending counter resource for TFO listen
	 * sockets as well as passively-created TFO sockets that transition
	 * from SYN_RECEIVED to CLOSED.
	 */
	if (tp->t_tfo_pending) {
		tcp_fastopen_decrement_counter(tp->t_tfo_pending);
		tp->t_tfo_pending = NULL;
	}
	in_pcbdrop(inp);
	TCPSTAT_INC(tcps_closed);
	if (tp->t_state != TCPS_CLOSED)
		tcp_state_change(tp, TCPS_CLOSED);
	KASSERT(inp->inp_socket != NULL, ("tcp_close: inp_socket NULL"));
	so = inp->inp_socket;
	soisdisconnected(so);
	if (inp->inp_flags & INP_SOCKREF) {
		KASSERT(so->so_state & SS_PROTOREF,
		    ("tcp_close: !SS_PROTOREF"));
		inp->inp_flags &= ~INP_SOCKREF;
		INP_WUNLOCK(inp);
		SOCK_LOCK(so);
		so->so_state &= ~SS_PROTOREF;
		sofree(so);
		return (NULL);
	}
	return (tp);
}

void
tcp_drain(void)
{
	VNET_ITERATOR_DECL(vnet_iter);

	if (!do_tcpdrain)
		return;

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		struct inpcb *inpb;
		struct tcpcb *tcpb;

	/*
	 * Walk the tcpbs, if existing, and flush the reassembly queue,
	 * if there is one...
	 * XXX: The "Net/3" implementation doesn't imply that the TCP
	 *      reassembly queue should be flushed, but in a situation
	 *	where we're really low on mbufs, this is potentially
	 *	useful.
	 */
		INP_INFO_WLOCK(&V_tcbinfo);
		CK_LIST_FOREACH(inpb, V_tcbinfo.ipi_listhead, inp_list) {
			INP_WLOCK(inpb);
			if (inpb->inp_flags & INP_TIMEWAIT) {
				INP_WUNLOCK(inpb);
				continue;
			}
			if ((tcpb = intotcpcb(inpb)) != NULL) {
				tcp_reass_flush(tcpb);
				tcp_clean_sackreport(tcpb);
#ifdef TCP_BLACKBOX
				tcp_log_drain(tcpb);
#endif
#ifdef TCPPCAP
				if (tcp_pcap_aggressive_free) {
					/* Free the TCP PCAP queues. */
					tcp_pcap_drain(&(tcpb->t_inpkts));
					tcp_pcap_drain(&(tcpb->t_outpkts));
				}
#endif
			}
			INP_WUNLOCK(inpb);
		}
		INP_INFO_WUNLOCK(&V_tcbinfo);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();
}

/*
 * Notify a tcp user of an asynchronous error;
 * store error as soft error, but wake up user
 * (for now, won't do anything until can select for soft error).
 *
 * Do not wake up user since there currently is no mechanism for
 * reporting soft errors (yet - a kqueue filter may be added).
 */
static struct inpcb *
tcp_notify(struct inpcb *inp, int error)
{
	struct tcpcb *tp;

	INP_INFO_LOCK_ASSERT(&V_tcbinfo);
	INP_WLOCK_ASSERT(inp);

	if ((inp->inp_flags & INP_TIMEWAIT) ||
	    (inp->inp_flags & INP_DROPPED))
		return (inp);

	tp = intotcpcb(inp);
	KASSERT(tp != NULL, ("tcp_notify: tp == NULL"));

	/*
	 * Ignore some errors if we are hooked up.
	 * If connection hasn't completed, has retransmitted several times,
	 * and receives a second error, give up now.  This is better
	 * than waiting a long time to establish a connection that
	 * can never complete.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	    (error == EHOSTUNREACH || error == ENETUNREACH ||
	     error == EHOSTDOWN)) {
		if (inp->inp_route.ro_rt) {
			RTFREE(inp->inp_route.ro_rt);
			inp->inp_route.ro_rt = (struct rtentry *)NULL;
		}
		return (inp);
	} else if (tp->t_state < TCPS_ESTABLISHED && tp->t_rxtshift > 3 &&
	    tp->t_softerror) {
		tp = tcp_drop(tp, error);
		if (tp != NULL)
			return (inp);
		else
			return (NULL);
	} else {
		tp->t_softerror = error;
		return (inp);
	}
#if 0
	wakeup( &so->so_timeo);
	sorwakeup(so);
	sowwakeup(so);
#endif
}

static int
tcp_pcblist(SYSCTL_HANDLER_ARGS)
{
	int error, i, m, n, pcb_count;
	struct inpcb *inp, **inp_list;
	inp_gen_t gencnt;
	struct xinpgen xig;
	struct epoch_tracker et;

	/*
	 * The process of preparing the TCB list is too time-consuming and
	 * resource-intensive to repeat twice on every request.
	 */
	if (req->oldptr == NULL) {
		n = V_tcbinfo.ipi_count +
		    counter_u64_fetch(V_tcps_states[TCPS_SYN_RECEIVED]);
		n += imax(n / 8, 10);
		req->oldidx = 2 * (sizeof xig) + n * sizeof(struct xtcpcb);
		return (0);
	}

	if (req->newptr != NULL)
		return (EPERM);

	/*
	 * OK, now we're committed to doing something.
	 */
	INP_LIST_RLOCK(&V_tcbinfo);
	gencnt = V_tcbinfo.ipi_gencnt;
	n = V_tcbinfo.ipi_count;
	INP_LIST_RUNLOCK(&V_tcbinfo);

	m = counter_u64_fetch(V_tcps_states[TCPS_SYN_RECEIVED]);

	error = sysctl_wire_old_buffer(req, 2 * (sizeof xig)
		+ (n + m) * sizeof(struct xtcpcb));
	if (error != 0)
		return (error);

	bzero(&xig, sizeof(xig));
	xig.xig_len = sizeof xig;
	xig.xig_count = n + m;
	xig.xig_gen = gencnt;
	xig.xig_sogen = so_gencnt;
	error = SYSCTL_OUT(req, &xig, sizeof xig);
	if (error)
		return (error);

	error = syncache_pcblist(req, m, &pcb_count);
	if (error)
		return (error);

	inp_list = malloc(n * sizeof *inp_list, M_TEMP, M_WAITOK);

	INP_INFO_WLOCK(&V_tcbinfo);
	for (inp = CK_LIST_FIRST(V_tcbinfo.ipi_listhead), i = 0;
	    inp != NULL && i < n; inp = CK_LIST_NEXT(inp, inp_list)) {
		INP_WLOCK(inp);
		if (inp->inp_gencnt <= gencnt) {
			/*
			 * XXX: This use of cr_cansee(), introduced with
			 * TCP state changes, is not quite right, but for
			 * now, better than nothing.
			 */
			if (inp->inp_flags & INP_TIMEWAIT) {
				if (intotw(inp) != NULL)
					error = cr_cansee(req->td->td_ucred,
					    intotw(inp)->tw_cred);
				else
					error = EINVAL;	/* Skip this inp. */
			} else
				error = cr_canseeinpcb(req->td->td_ucred, inp);
			if (error == 0) {
				in_pcbref(inp);
				inp_list[i++] = inp;
			}
		}
		INP_WUNLOCK(inp);
	}
	INP_INFO_WUNLOCK(&V_tcbinfo);
	n = i;

	error = 0;
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (inp->inp_gencnt <= gencnt) {
			struct xtcpcb xt;

			tcp_inptoxtp(inp, &xt);
			INP_RUNLOCK(inp);
			error = SYSCTL_OUT(req, &xt, sizeof xt);
		} else
			INP_RUNLOCK(inp);
	}
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	for (i = 0; i < n; i++) {
		inp = inp_list[i];
		INP_RLOCK(inp);
		if (!in_pcbrele_rlocked(inp))
			INP_RUNLOCK(inp);
	}
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);

	if (!error) {
		/*
		 * Give the user an updated idea of our state.
		 * If the generation differs from what we told
		 * her before, she knows that something happened
		 * while we were processing this request, and it
		 * might be necessary to retry.
		 */
		INP_LIST_RLOCK(&V_tcbinfo);
		xig.xig_gen = V_tcbinfo.ipi_gencnt;
		xig.xig_sogen = so_gencnt;
		xig.xig_count = V_tcbinfo.ipi_count + pcb_count;
		INP_LIST_RUNLOCK(&V_tcbinfo);
		error = SYSCTL_OUT(req, &xig, sizeof xig);
	}
	free(inp_list, M_TEMP);
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, TCPCTL_PCBLIST, pcblist,
    CTLTYPE_OPAQUE | CTLFLAG_RD, NULL, 0,
    tcp_pcblist, "S,xtcpcb", "List of active TCP connections");

#ifdef INET
static int
tcp_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in addrs[2];
	struct inpcb *inp;
	int error;

	error = priv_check(req->td, PRIV_NETINET_GETCRED);
	if (error)
		return (error);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	inp = in_pcblookup(&V_tcbinfo, addrs[1].sin_addr, addrs[1].sin_port,
	    addrs[0].sin_addr, addrs[0].sin_port, INPLOOKUP_RLOCKPCB, NULL);
	if (inp != NULL) {
		if (inp->inp_socket == NULL)
			error = ENOENT;
		if (error == 0)
			error = cr_canseeinpcb(req->td->td_ucred, inp);
		if (error == 0)
			cru2x(inp->inp_cred, &xuc);
		INP_RUNLOCK(inp);
	} else
		error = ENOENT;
	if (error == 0)
		error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, OID_AUTO, getcred,
    CTLTYPE_OPAQUE|CTLFLAG_RW|CTLFLAG_PRISON, 0, 0,
    tcp_getcred, "S,xucred", "Get the xucred of a TCP connection");
#endif /* INET */

#ifdef INET6
static int
tcp6_getcred(SYSCTL_HANDLER_ARGS)
{
	struct xucred xuc;
	struct sockaddr_in6 addrs[2];
	struct inpcb *inp;
	int error;
#ifdef INET
	int mapped = 0;
#endif

	error = priv_check(req->td, PRIV_NETINET_GETCRED);
	if (error)
		return (error);
	error = SYSCTL_IN(req, addrs, sizeof(addrs));
	if (error)
		return (error);
	if ((error = sa6_embedscope(&addrs[0], V_ip6_use_defzone)) != 0 ||
	    (error = sa6_embedscope(&addrs[1], V_ip6_use_defzone)) != 0) {
		return (error);
	}
	if (IN6_IS_ADDR_V4MAPPED(&addrs[0].sin6_addr)) {
#ifdef INET
		if (IN6_IS_ADDR_V4MAPPED(&addrs[1].sin6_addr))
			mapped = 1;
		else
#endif
			return (EINVAL);
	}

#ifdef INET
	if (mapped == 1)
		inp = in_pcblookup(&V_tcbinfo,
			*(struct in_addr *)&addrs[1].sin6_addr.s6_addr[12],
			addrs[1].sin6_port,
			*(struct in_addr *)&addrs[0].sin6_addr.s6_addr[12],
			addrs[0].sin6_port, INPLOOKUP_RLOCKPCB, NULL);
	else
#endif
		inp = in6_pcblookup(&V_tcbinfo,
			&addrs[1].sin6_addr, addrs[1].sin6_port,
			&addrs[0].sin6_addr, addrs[0].sin6_port,
			INPLOOKUP_RLOCKPCB, NULL);
	if (inp != NULL) {
		if (inp->inp_socket == NULL)
			error = ENOENT;
		if (error == 0)
			error = cr_canseeinpcb(req->td->td_ucred, inp);
		if (error == 0)
			cru2x(inp->inp_cred, &xuc);
		INP_RUNLOCK(inp);
	} else
		error = ENOENT;
	if (error == 0)
		error = SYSCTL_OUT(req, &xuc, sizeof(struct xucred));
	return (error);
}

SYSCTL_PROC(_net_inet6_tcp6, OID_AUTO, getcred,
    CTLTYPE_OPAQUE|CTLFLAG_RW|CTLFLAG_PRISON, 0, 0,
    tcp6_getcred, "S,xucred", "Get the xucred of a TCP6 connection");
#endif /* INET6 */


#ifdef INET
void
tcp_ctlinput(int cmd, struct sockaddr *sa, void *vip)
{
	struct ip *ip = vip;
	struct tcphdr *th;
	struct in_addr faddr;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct inpcb *(*notify)(struct inpcb *, int) = tcp_notify;
	struct icmp *icp;
	struct in_conninfo inc;
	struct epoch_tracker et;
	tcp_seq icmp_tcp_seq;
	int mtu;

	faddr = ((struct sockaddr_in *)sa)->sin_addr;
	if (sa->sa_family != AF_INET || faddr.s_addr == INADDR_ANY)
		return;

	if (cmd == PRC_MSGSIZE)
		notify = tcp_mtudisc_notify;
	else if (V_icmp_may_rst && (cmd == PRC_UNREACH_ADMIN_PROHIB ||
		cmd == PRC_UNREACH_PORT || cmd == PRC_UNREACH_PROTOCOL || 
		cmd == PRC_TIMXCEED_INTRANS) && ip)
		notify = tcp_drop_syn_sent;

	/*
	 * Hostdead is ugly because it goes linearly through all PCBs.
	 * XXX: We never get this from ICMP, otherwise it makes an
	 * excellent DoS attack on machines with many connections.
	 */
	else if (cmd == PRC_HOSTDEAD)
		ip = NULL;
	else if ((unsigned)cmd >= PRC_NCMDS || inetctlerrmap[cmd] == 0)
		return;

	if (ip == NULL) {
		in_pcbnotifyall(&V_tcbinfo, faddr, inetctlerrmap[cmd], notify);
		return;
	}

	icp = (struct icmp *)((caddr_t)ip - offsetof(struct icmp, icmp_ip));
	th = (struct tcphdr *)((caddr_t)ip + (ip->ip_hl << 2));
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	inp = in_pcblookup(&V_tcbinfo, faddr, th->th_dport, ip->ip_src,
	    th->th_sport, INPLOOKUP_WLOCKPCB, NULL);
	if (inp != NULL && PRC_IS_REDIRECT(cmd)) {
		/* signal EHOSTDOWN, as it flushes the cached route */
		inp = (*notify)(inp, EHOSTDOWN);
		goto out;
	}
	icmp_tcp_seq = th->th_seq;
	if (inp != NULL)  {
		if (!(inp->inp_flags & INP_TIMEWAIT) &&
		    !(inp->inp_flags & INP_DROPPED) &&
		    !(inp->inp_socket == NULL)) {
			tp = intotcpcb(inp);
			if (SEQ_GEQ(ntohl(icmp_tcp_seq), tp->snd_una) &&
			    SEQ_LT(ntohl(icmp_tcp_seq), tp->snd_max)) {
				if (cmd == PRC_MSGSIZE) {
					/*
					 * MTU discovery:
					 * If we got a needfrag set the MTU
					 * in the route to the suggested new
					 * value (if given) and then notify.
					 */
					mtu = ntohs(icp->icmp_nextmtu);
					/*
					 * If no alternative MTU was
					 * proposed, try the next smaller
					 * one.
					 */
					if (!mtu)
						mtu = ip_next_mtu(
						    ntohs(ip->ip_len), 1);
					if (mtu < V_tcp_minmss +
					    sizeof(struct tcpiphdr))
						mtu = V_tcp_minmss +
						    sizeof(struct tcpiphdr);
					/*
					 * Only process the offered MTU if it
					 * is smaller than the current one.
					 */
					if (mtu < tp->t_maxseg +
					    sizeof(struct tcpiphdr)) {
						bzero(&inc, sizeof(inc));
						inc.inc_faddr = faddr;
						inc.inc_fibnum =
						    inp->inp_inc.inc_fibnum;
						tcp_hc_updatemtu(&inc, mtu);
						tcp_mtudisc(inp, mtu);
					}
				} else
					inp = (*notify)(inp,
					    inetctlerrmap[cmd]);
			}
		}
	} else {
		bzero(&inc, sizeof(inc));
		inc.inc_fport = th->th_dport;
		inc.inc_lport = th->th_sport;
		inc.inc_faddr = faddr;
		inc.inc_laddr = ip->ip_src;
		syncache_unreach(&inc, icmp_tcp_seq);
	}
out:
	if (inp != NULL)
		INP_WUNLOCK(inp);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
}
#endif /* INET */

#ifdef INET6
void
tcp6_ctlinput(int cmd, struct sockaddr *sa, void *d)
{
	struct in6_addr *dst;
	struct inpcb *(*notify)(struct inpcb *, int) = tcp_notify;
	struct ip6_hdr *ip6;
	struct mbuf *m;
	struct inpcb *inp;
	struct tcpcb *tp;
	struct icmp6_hdr *icmp6;
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	struct in_conninfo inc;
	struct epoch_tracker et;
	struct tcp_ports {
		uint16_t th_sport;
		uint16_t th_dport;
	} t_ports;
	tcp_seq icmp_tcp_seq;
	unsigned int mtu;
	unsigned int off;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		icmp6 = ip6cp->ip6c_icmp6;
		m = ip6cp->ip6c_m;
		ip6 = ip6cp->ip6c_ip6;
		off = ip6cp->ip6c_off;
		sa6_src = ip6cp->ip6c_src;
		dst = ip6cp->ip6c_finaldst;
	} else {
		m = NULL;
		ip6 = NULL;
		off = 0;	/* fool gcc */
		sa6_src = &sa6_any;
		dst = NULL;
	}

	if (cmd == PRC_MSGSIZE)
		notify = tcp_mtudisc_notify;
	else if (V_icmp_may_rst && (cmd == PRC_UNREACH_ADMIN_PROHIB ||
		cmd == PRC_UNREACH_PORT || cmd == PRC_UNREACH_PROTOCOL || 
		cmd == PRC_TIMXCEED_INTRANS) && ip6 != NULL)
		notify = tcp_drop_syn_sent;

	/*
	 * Hostdead is ugly because it goes linearly through all PCBs.
	 * XXX: We never get this from ICMP, otherwise it makes an
	 * excellent DoS attack on machines with many connections.
	 */
	else if (cmd == PRC_HOSTDEAD)
		ip6 = NULL;
	else if ((unsigned)cmd >= PRC_NCMDS || inet6ctlerrmap[cmd] == 0)
		return;

	if (ip6 == NULL) {
		in6_pcbnotify(&V_tcbinfo, sa, 0,
			      (const struct sockaddr *)sa6_src,
			      0, cmd, NULL, notify);
		return;
	}

	/* Check if we can safely get the ports from the tcp hdr */
	if (m == NULL ||
	    (m->m_pkthdr.len <
		(int32_t) (off + sizeof(struct tcp_ports)))) {
		return;
	}
	bzero(&t_ports, sizeof(struct tcp_ports));
	m_copydata(m, off, sizeof(struct tcp_ports), (caddr_t)&t_ports);
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	inp = in6_pcblookup(&V_tcbinfo, &ip6->ip6_dst, t_ports.th_dport,
	    &ip6->ip6_src, t_ports.th_sport, INPLOOKUP_WLOCKPCB, NULL);
	if (inp != NULL && PRC_IS_REDIRECT(cmd)) {
		/* signal EHOSTDOWN, as it flushes the cached route */
		inp = (*notify)(inp, EHOSTDOWN);
		goto out;
	}
	off += sizeof(struct tcp_ports);
	if (m->m_pkthdr.len < (int32_t) (off + sizeof(tcp_seq))) {
		goto out;
	}
	m_copydata(m, off, sizeof(tcp_seq), (caddr_t)&icmp_tcp_seq);
	if (inp != NULL)  {
		if (!(inp->inp_flags & INP_TIMEWAIT) &&
		    !(inp->inp_flags & INP_DROPPED) &&
		    !(inp->inp_socket == NULL)) {
			tp = intotcpcb(inp);
			if (SEQ_GEQ(ntohl(icmp_tcp_seq), tp->snd_una) &&
			    SEQ_LT(ntohl(icmp_tcp_seq), tp->snd_max)) {
				if (cmd == PRC_MSGSIZE) {
					/*
					 * MTU discovery:
					 * If we got a needfrag set the MTU
					 * in the route to the suggested new
					 * value (if given) and then notify.
					 */
					mtu = ntohl(icmp6->icmp6_mtu);
					/*
					 * If no alternative MTU was
					 * proposed, or the proposed
					 * MTU was too small, set to
					 * the min.
					 */
					if (mtu < IPV6_MMTU)
						mtu = IPV6_MMTU - 8;
					bzero(&inc, sizeof(inc));
					inc.inc_fibnum = M_GETFIB(m);
					inc.inc_flags |= INC_ISIPV6;
					inc.inc6_faddr = *dst;
					if (in6_setscope(&inc.inc6_faddr,
						m->m_pkthdr.rcvif, NULL))
						goto out;
					/*
					 * Only process the offered MTU if it
					 * is smaller than the current one.
					 */
					if (mtu < tp->t_maxseg +
					    sizeof (struct tcphdr) +
					    sizeof (struct ip6_hdr)) {
						tcp_hc_updatemtu(&inc, mtu);
						tcp_mtudisc(inp, mtu);
						ICMP6STAT_INC(icp6s_pmtuchg);
					}
				} else
					inp = (*notify)(inp,
					    inet6ctlerrmap[cmd]);
			}
		}
	} else {
		bzero(&inc, sizeof(inc));
		inc.inc_fibnum = M_GETFIB(m);
		inc.inc_flags |= INC_ISIPV6;
		inc.inc_fport = t_ports.th_dport;
		inc.inc_lport = t_ports.th_sport;
		inc.inc6_faddr = *dst;
		inc.inc6_laddr = ip6->ip6_src;
		syncache_unreach(&inc, icmp_tcp_seq);
	}
out:
	if (inp != NULL)
		INP_WUNLOCK(inp);
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
}
#endif /* INET6 */

static uint32_t
tcp_keyed_hash(struct in_conninfo *inc, u_char *key, u_int len)
{
	MD5_CTX ctx;
	uint32_t hash[4];

	MD5Init(&ctx);
	MD5Update(&ctx, &inc->inc_fport, sizeof(uint16_t));
	MD5Update(&ctx, &inc->inc_lport, sizeof(uint16_t));
	switch (inc->inc_flags & INC_ISIPV6) {
#ifdef INET
	case 0:
		MD5Update(&ctx, &inc->inc_faddr, sizeof(struct in_addr));
		MD5Update(&ctx, &inc->inc_laddr, sizeof(struct in_addr));
		break;
#endif
#ifdef INET6
	case INC_ISIPV6:
		MD5Update(&ctx, &inc->inc6_faddr, sizeof(struct in6_addr));
		MD5Update(&ctx, &inc->inc6_laddr, sizeof(struct in6_addr));
		break;
#endif
	}
	MD5Update(&ctx, key, len);
	MD5Final((unsigned char *)hash, &ctx);

	return (hash[0]);
}

uint32_t
tcp_new_ts_offset(struct in_conninfo *inc)
{
	return (tcp_keyed_hash(inc, V_ts_offset_secret,
	    sizeof(V_ts_offset_secret)));
}

/*
 * Following is where TCP initial sequence number generation occurs.
 *
 * There are two places where we must use initial sequence numbers:
 * 1.  In SYN-ACK packets.
 * 2.  In SYN packets.
 *
 * All ISNs for SYN-ACK packets are generated by the syncache.  See
 * tcp_syncache.c for details.
 *
 * The ISNs in SYN packets must be monotonic; TIME_WAIT recycling
 * depends on this property.  In addition, these ISNs should be
 * unguessable so as to prevent connection hijacking.  To satisfy
 * the requirements of this situation, the algorithm outlined in
 * RFC 1948 is used, with only small modifications.
 *
 * Implementation details:
 *
 * Time is based off the system timer, and is corrected so that it
 * increases by one megabyte per second.  This allows for proper
 * recycling on high speed LANs while still leaving over an hour
 * before rollover.
 *
 * As reading the *exact* system time is too expensive to be done
 * whenever setting up a TCP connection, we increment the time
 * offset in two ways.  First, a small random positive increment
 * is added to isn_offset for each connection that is set up.
 * Second, the function tcp_isn_tick fires once per clock tick
 * and increments isn_offset as necessary so that sequence numbers
 * are incremented at approximately ISN_BYTES_PER_SECOND.  The
 * random positive increments serve only to ensure that the same
 * exact sequence number is never sent out twice (as could otherwise
 * happen when a port is recycled in less than the system tick
 * interval.)
 *
 * net.inet.tcp.isn_reseed_interval controls the number of seconds
 * between seeding of isn_secret.  This is normally set to zero,
 * as reseeding should not be necessary.
 *
 * Locking of the global variables isn_secret, isn_last_reseed, isn_offset,
 * isn_offset_old, and isn_ctx is performed using the ISN lock.  In
 * general, this means holding an exclusive (write) lock.
 */

#define ISN_BYTES_PER_SECOND 1048576
#define ISN_STATIC_INCREMENT 4096
#define ISN_RANDOM_INCREMENT (4096 - 1)
#define ISN_SECRET_LENGTH    32

VNET_DEFINE_STATIC(u_char, isn_secret[ISN_SECRET_LENGTH]);
VNET_DEFINE_STATIC(int, isn_last);
VNET_DEFINE_STATIC(int, isn_last_reseed);
VNET_DEFINE_STATIC(u_int32_t, isn_offset);
VNET_DEFINE_STATIC(u_int32_t, isn_offset_old);

#define	V_isn_secret			VNET(isn_secret)
#define	V_isn_last			VNET(isn_last)
#define	V_isn_last_reseed		VNET(isn_last_reseed)
#define	V_isn_offset			VNET(isn_offset)
#define	V_isn_offset_old		VNET(isn_offset_old)

tcp_seq
tcp_new_isn(struct in_conninfo *inc)
{
	tcp_seq new_isn;
	u_int32_t projected_offset;

	ISN_LOCK();
	/* Seed if this is the first use, reseed if requested. */
	if ((V_isn_last_reseed == 0) || ((V_tcp_isn_reseed_interval > 0) &&
	     (((u_int)V_isn_last_reseed + (u_int)V_tcp_isn_reseed_interval*hz)
		< (u_int)ticks))) {
		arc4rand(&V_isn_secret, sizeof(V_isn_secret), 0);
		V_isn_last_reseed = ticks;
	}

	/* Compute the md5 hash and return the ISN. */
	new_isn = (tcp_seq)tcp_keyed_hash(inc, V_isn_secret,
	    sizeof(V_isn_secret));
	V_isn_offset += ISN_STATIC_INCREMENT +
		(arc4random() & ISN_RANDOM_INCREMENT);
	if (ticks != V_isn_last) {
		projected_offset = V_isn_offset_old +
		    ISN_BYTES_PER_SECOND / hz * (ticks - V_isn_last);
		if (SEQ_GT(projected_offset, V_isn_offset))
			V_isn_offset = projected_offset;
		V_isn_offset_old = V_isn_offset;
		V_isn_last = ticks;
	}
	new_isn += V_isn_offset;
	ISN_UNLOCK();
	return (new_isn);
}

/*
 * When a specific ICMP unreachable message is received and the
 * connection state is SYN-SENT, drop the connection.  This behavior
 * is controlled by the icmp_may_rst sysctl.
 */
struct inpcb *
tcp_drop_syn_sent(struct inpcb *inp, int errno)
{
	struct tcpcb *tp;

	INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
	INP_WLOCK_ASSERT(inp);

	if ((inp->inp_flags & INP_TIMEWAIT) ||
	    (inp->inp_flags & INP_DROPPED))
		return (inp);

	tp = intotcpcb(inp);
	if (tp->t_state != TCPS_SYN_SENT)
		return (inp);

	if (IS_FASTOPEN(tp->t_flags))
		tcp_fastopen_disable_path(tp);
	
	tp = tcp_drop(tp, errno);
	if (tp != NULL)
		return (inp);
	else
		return (NULL);
}

/*
 * When `need fragmentation' ICMP is received, update our idea of the MSS
 * based on the new value. Also nudge TCP to send something, since we
 * know the packet we just sent was dropped.
 * This duplicates some code in the tcp_mss() function in tcp_input.c.
 */
static struct inpcb *
tcp_mtudisc_notify(struct inpcb *inp, int error)
{

	tcp_mtudisc(inp, -1);
	return (inp);
}

static void
tcp_mtudisc(struct inpcb *inp, int mtuoffer)
{
	struct tcpcb *tp;
	struct socket *so;

	INP_WLOCK_ASSERT(inp);
	if ((inp->inp_flags & INP_TIMEWAIT) ||
	    (inp->inp_flags & INP_DROPPED))
		return;

	tp = intotcpcb(inp);
	KASSERT(tp != NULL, ("tcp_mtudisc: tp == NULL"));

	tcp_mss_update(tp, -1, mtuoffer, NULL, NULL);
  
	so = inp->inp_socket;
	SOCKBUF_LOCK(&so->so_snd);
	/* If the mss is larger than the socket buffer, decrease the mss. */
	if (so->so_snd.sb_hiwat < tp->t_maxseg)
		tp->t_maxseg = so->so_snd.sb_hiwat;
	SOCKBUF_UNLOCK(&so->so_snd);

	TCPSTAT_INC(tcps_mturesent);
	tp->t_rtttime = 0;
	tp->snd_nxt = tp->snd_una;
	tcp_free_sackholes(tp);
	tp->snd_recover = tp->snd_max;
	if (tp->t_flags & TF_SACK_PERMIT)
		EXIT_FASTRECOVERY(tp->t_flags);
	tp->t_fb->tfb_tcp_output(tp);
}

#ifdef INET
/*
 * Look-up the routing entry to the peer of this inpcb.  If no route
 * is found and it cannot be allocated, then return 0.  This routine
 * is called by TCP routines that access the rmx structure and by
 * tcp_mss_update to get the peer/interface MTU.
 */
uint32_t
tcp_maxmtu(struct in_conninfo *inc, struct tcp_ifcap *cap)
{
	struct nhop4_extended nh4;
	struct ifnet *ifp;
	uint32_t maxmtu = 0;

	KASSERT(inc != NULL, ("tcp_maxmtu with NULL in_conninfo pointer"));

	if (inc->inc_faddr.s_addr != INADDR_ANY) {

		if (fib4_lookup_nh_ext(inc->inc_fibnum, inc->inc_faddr,
		    NHR_REF, 0, &nh4) != 0)
			return (0);

		ifp = nh4.nh_ifp;
		maxmtu = nh4.nh_mtu;

		/* Report additional interface capabilities. */
		if (cap != NULL) {
			if (ifp->if_capenable & IFCAP_TSO4 &&
			    ifp->if_hwassist & CSUM_TSO) {
				cap->ifcap |= CSUM_TSO;
				cap->tsomax = ifp->if_hw_tsomax;
				cap->tsomaxsegcount = ifp->if_hw_tsomaxsegcount;
				cap->tsomaxsegsize = ifp->if_hw_tsomaxsegsize;
			}
		}
		fib4_free_nh_ext(inc->inc_fibnum, &nh4);
	}
	return (maxmtu);
}
#endif /* INET */

#ifdef INET6
uint32_t
tcp_maxmtu6(struct in_conninfo *inc, struct tcp_ifcap *cap)
{
	struct nhop6_extended nh6;
	struct in6_addr dst6;
	uint32_t scopeid;
	struct ifnet *ifp;
	uint32_t maxmtu = 0;

	KASSERT(inc != NULL, ("tcp_maxmtu6 with NULL in_conninfo pointer"));

	if (inc->inc_flags & INC_IPV6MINMTU)
		return (IPV6_MMTU);

	if (!IN6_IS_ADDR_UNSPECIFIED(&inc->inc6_faddr)) {
		in6_splitscope(&inc->inc6_faddr, &dst6, &scopeid);
		if (fib6_lookup_nh_ext(inc->inc_fibnum, &dst6, scopeid, 0,
		    0, &nh6) != 0)
			return (0);

		ifp = nh6.nh_ifp;
		maxmtu = nh6.nh_mtu;

		/* Report additional interface capabilities. */
		if (cap != NULL) {
			if (ifp->if_capenable & IFCAP_TSO6 &&
			    ifp->if_hwassist & CSUM_TSO) {
				cap->ifcap |= CSUM_TSO;
				cap->tsomax = ifp->if_hw_tsomax;
				cap->tsomaxsegcount = ifp->if_hw_tsomaxsegcount;
				cap->tsomaxsegsize = ifp->if_hw_tsomaxsegsize;
			}
		}
		fib6_free_nh_ext(inc->inc_fibnum, &nh6);
	}

	return (maxmtu);
}
#endif /* INET6 */

/*
 * Calculate effective SMSS per RFC5681 definition for a given TCP
 * connection at its current state, taking into account SACK and etc.
 */
u_int
tcp_maxseg(const struct tcpcb *tp)
{
	u_int optlen;

	if (tp->t_flags & TF_NOOPT)
		return (tp->t_maxseg);

	/*
	 * Here we have a simplified code from tcp_addoptions(),
	 * without a proper loop, and having most of paddings hardcoded.
	 * We might make mistakes with padding here in some edge cases,
	 * but this is harmless, since result of tcp_maxseg() is used
	 * only in cwnd and ssthresh estimations.
	 */
#define	PAD(len)	((((len) / 4) + !!((len) % 4)) * 4)
	if (TCPS_HAVEESTABLISHED(tp->t_state)) {
		if (tp->t_flags & TF_RCVD_TSTMP)
			optlen = TCPOLEN_TSTAMP_APPA;
		else
			optlen = 0;
#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
		if (tp->t_flags & TF_SIGNATURE)
			optlen += PAD(TCPOLEN_SIGNATURE);
#endif
		if ((tp->t_flags & TF_SACK_PERMIT) && tp->rcv_numsacks > 0) {
			optlen += TCPOLEN_SACKHDR;
			optlen += tp->rcv_numsacks * TCPOLEN_SACK;
			optlen = PAD(optlen);
		}
	} else {
		if (tp->t_flags & TF_REQ_TSTMP)
			optlen = TCPOLEN_TSTAMP_APPA;
		else
			optlen = PAD(TCPOLEN_MAXSEG);
		if (tp->t_flags & TF_REQ_SCALE)
			optlen += PAD(TCPOLEN_WINDOW);
#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
		if (tp->t_flags & TF_SIGNATURE)
			optlen += PAD(TCPOLEN_SIGNATURE);
#endif
		if (tp->t_flags & TF_SACK_PERMIT)
			optlen += PAD(TCPOLEN_SACK_PERMITTED);
	}
#undef PAD
	optlen = min(optlen, TCP_MAXOLEN);
	return (tp->t_maxseg - optlen);
}

static int
sysctl_drop(SYSCTL_HANDLER_ARGS)
{
	/* addrs[0] is a foreign socket, addrs[1] is a local one. */
	struct sockaddr_storage addrs[2];
	struct inpcb *inp;
	struct tcpcb *tp;
	struct tcptw *tw;
	struct sockaddr_in *fin, *lin;
	struct epoch_tracker et;
#ifdef INET6
	struct sockaddr_in6 *fin6, *lin6;
#endif
	int error;

	inp = NULL;
	fin = lin = NULL;
#ifdef INET6
	fin6 = lin6 = NULL;
#endif
	error = 0;

	if (req->oldptr != NULL || req->oldlen != 0)
		return (EINVAL);
	if (req->newptr == NULL)
		return (EPERM);
	if (req->newlen < sizeof(addrs))
		return (ENOMEM);
	error = SYSCTL_IN(req, &addrs, sizeof(addrs));
	if (error)
		return (error);

	switch (addrs[0].ss_family) {
#ifdef INET6
	case AF_INET6:
		fin6 = (struct sockaddr_in6 *)&addrs[0];
		lin6 = (struct sockaddr_in6 *)&addrs[1];
		if (fin6->sin6_len != sizeof(struct sockaddr_in6) ||
		    lin6->sin6_len != sizeof(struct sockaddr_in6))
			return (EINVAL);
		if (IN6_IS_ADDR_V4MAPPED(&fin6->sin6_addr)) {
			if (!IN6_IS_ADDR_V4MAPPED(&lin6->sin6_addr))
				return (EINVAL);
			in6_sin6_2_sin_in_sock((struct sockaddr *)&addrs[0]);
			in6_sin6_2_sin_in_sock((struct sockaddr *)&addrs[1]);
			fin = (struct sockaddr_in *)&addrs[0];
			lin = (struct sockaddr_in *)&addrs[1];
			break;
		}
		error = sa6_embedscope(fin6, V_ip6_use_defzone);
		if (error)
			return (error);
		error = sa6_embedscope(lin6, V_ip6_use_defzone);
		if (error)
			return (error);
		break;
#endif
#ifdef INET
	case AF_INET:
		fin = (struct sockaddr_in *)&addrs[0];
		lin = (struct sockaddr_in *)&addrs[1];
		if (fin->sin_len != sizeof(struct sockaddr_in) ||
		    lin->sin_len != sizeof(struct sockaddr_in))
			return (EINVAL);
		break;
#endif
	default:
		return (EINVAL);
	}
	INP_INFO_RLOCK_ET(&V_tcbinfo, et);
	switch (addrs[0].ss_family) {
#ifdef INET6
	case AF_INET6:
		inp = in6_pcblookup(&V_tcbinfo, &fin6->sin6_addr,
		    fin6->sin6_port, &lin6->sin6_addr, lin6->sin6_port,
		    INPLOOKUP_WLOCKPCB, NULL);
		break;
#endif
#ifdef INET
	case AF_INET:
		inp = in_pcblookup(&V_tcbinfo, fin->sin_addr, fin->sin_port,
		    lin->sin_addr, lin->sin_port, INPLOOKUP_WLOCKPCB, NULL);
		break;
#endif
	}
	if (inp != NULL) {
		if (inp->inp_flags & INP_TIMEWAIT) {
			/*
			 * XXXRW: There currently exists a state where an
			 * inpcb is present, but its timewait state has been
			 * discarded.  For now, don't allow dropping of this
			 * type of inpcb.
			 */
			tw = intotw(inp);
			if (tw != NULL)
				tcp_twclose(tw, 0);
			else
				INP_WUNLOCK(inp);
		} else if (!(inp->inp_flags & INP_DROPPED) &&
			   !(inp->inp_socket->so_options & SO_ACCEPTCONN)) {
			tp = intotcpcb(inp);
			tp = tcp_drop(tp, ECONNABORTED);
			if (tp != NULL)
				INP_WUNLOCK(inp);
		} else
			INP_WUNLOCK(inp);
	} else
		error = ESRCH;
	INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
	return (error);
}

SYSCTL_PROC(_net_inet_tcp, TCPCTL_DROP, drop,
    CTLFLAG_VNET | CTLTYPE_STRUCT | CTLFLAG_WR | CTLFLAG_SKIP, NULL,
    0, sysctl_drop, "", "Drop TCP connection");

/*
 * Generate a standardized TCP log line for use throughout the
 * tcp subsystem.  Memory allocation is done with M_NOWAIT to
 * allow use in the interrupt context.
 *
 * NB: The caller MUST free(s, M_TCPLOG) the returned string.
 * NB: The function may return NULL if memory allocation failed.
 *
 * Due to header inclusion and ordering limitations the struct ip
 * and ip6_hdr pointers have to be passed as void pointers.
 */
char *
tcp_log_vain(struct in_conninfo *inc, struct tcphdr *th, void *ip4hdr,
    const void *ip6hdr)
{

	/* Is logging enabled? */
	if (tcp_log_in_vain == 0)
		return (NULL);

	return (tcp_log_addr(inc, th, ip4hdr, ip6hdr));
}

char *
tcp_log_addrs(struct in_conninfo *inc, struct tcphdr *th, void *ip4hdr,
    const void *ip6hdr)
{

	/* Is logging enabled? */
	if (tcp_log_debug == 0)
		return (NULL);

	return (tcp_log_addr(inc, th, ip4hdr, ip6hdr));
}

static char *
tcp_log_addr(struct in_conninfo *inc, struct tcphdr *th, void *ip4hdr,
    const void *ip6hdr)
{
	char *s, *sp;
	size_t size;
	struct ip *ip;
#ifdef INET6
	const struct ip6_hdr *ip6;

	ip6 = (const struct ip6_hdr *)ip6hdr;
#endif /* INET6 */
	ip = (struct ip *)ip4hdr;

	/*
	 * The log line looks like this:
	 * "TCP: [1.2.3.4]:50332 to [1.2.3.4]:80 tcpflags 0x2<SYN>"
	 */
	size = sizeof("TCP: []:12345 to []:12345 tcpflags 0x2<>") +
	    sizeof(PRINT_TH_FLAGS) + 1 +
#ifdef INET6
	    2 * INET6_ADDRSTRLEN;
#else
	    2 * INET_ADDRSTRLEN;
#endif /* INET6 */

	s = malloc(size, M_TCPLOG, M_ZERO|M_NOWAIT);
	if (s == NULL)
		return (NULL);

	strcat(s, "TCP: [");
	sp = s + strlen(s);

	if (inc && ((inc->inc_flags & INC_ISIPV6) == 0)) {
		inet_ntoa_r(inc->inc_faddr, sp);
		sp = s + strlen(s);
		sprintf(sp, "]:%i to [", ntohs(inc->inc_fport));
		sp = s + strlen(s);
		inet_ntoa_r(inc->inc_laddr, sp);
		sp = s + strlen(s);
		sprintf(sp, "]:%i", ntohs(inc->inc_lport));
#ifdef INET6
	} else if (inc) {
		ip6_sprintf(sp, &inc->inc6_faddr);
		sp = s + strlen(s);
		sprintf(sp, "]:%i to [", ntohs(inc->inc_fport));
		sp = s + strlen(s);
		ip6_sprintf(sp, &inc->inc6_laddr);
		sp = s + strlen(s);
		sprintf(sp, "]:%i", ntohs(inc->inc_lport));
	} else if (ip6 && th) {
		ip6_sprintf(sp, &ip6->ip6_src);
		sp = s + strlen(s);
		sprintf(sp, "]:%i to [", ntohs(th->th_sport));
		sp = s + strlen(s);
		ip6_sprintf(sp, &ip6->ip6_dst);
		sp = s + strlen(s);
		sprintf(sp, "]:%i", ntohs(th->th_dport));
#endif /* INET6 */
#ifdef INET
	} else if (ip && th) {
		inet_ntoa_r(ip->ip_src, sp);
		sp = s + strlen(s);
		sprintf(sp, "]:%i to [", ntohs(th->th_sport));
		sp = s + strlen(s);
		inet_ntoa_r(ip->ip_dst, sp);
		sp = s + strlen(s);
		sprintf(sp, "]:%i", ntohs(th->th_dport));
#endif /* INET */
	} else {
		free(s, M_TCPLOG);
		return (NULL);
	}
	sp = s + strlen(s);
	if (th)
		sprintf(sp, " tcpflags 0x%b", th->th_flags, PRINT_TH_FLAGS);
	if (*(s + size - 1) != '\0')
		panic("%s: string too long", __func__);
	return (s);
}

/*
 * A subroutine which makes it easy to track TCP state changes with DTrace.
 * This function shouldn't be called for t_state initializations that don't
 * correspond to actual TCP state transitions.
 */
void
tcp_state_change(struct tcpcb *tp, int newstate)
{
#if defined(KDTRACE_HOOKS)
	int pstate = tp->t_state;
#endif

	TCPSTATES_DEC(tp->t_state);
	TCPSTATES_INC(newstate);
	tp->t_state = newstate;
	TCP_PROBE6(state__change, NULL, tp, NULL, tp, NULL, pstate);
}

/*
 * Create an external-format (``xtcpcb'') structure using the information in
 * the kernel-format tcpcb structure pointed to by tp.  This is done to
 * reduce the spew of irrelevant information over this interface, to isolate
 * user code from changes in the kernel structure, and potentially to provide
 * information-hiding if we decide that some of this information should be
 * hidden from users.
 */
void
tcp_inptoxtp(const struct inpcb *inp, struct xtcpcb *xt)
{
	struct tcpcb *tp = intotcpcb(inp);
	sbintime_t now;

	bzero(xt, sizeof(*xt));
	if (inp->inp_flags & INP_TIMEWAIT) {
		xt->t_state = TCPS_TIME_WAIT;
	} else {
		xt->t_state = tp->t_state;
		xt->t_logstate = tp->t_logstate;
		xt->t_flags = tp->t_flags;
		xt->t_sndzerowin = tp->t_sndzerowin;
		xt->t_sndrexmitpack = tp->t_sndrexmitpack;
		xt->t_rcvoopack = tp->t_rcvoopack;

		now = getsbinuptime();
#define	COPYTIMER(ttt)	do {						\
		if (callout_active(&tp->t_timers->ttt))			\
			xt->ttt = (tp->t_timers->ttt.c_time - now) /	\
			    SBT_1MS;					\
		else							\
			xt->ttt = 0;					\
} while (0)
		COPYTIMER(tt_delack);
		COPYTIMER(tt_rexmt);
		COPYTIMER(tt_persist);
		COPYTIMER(tt_keep);
		COPYTIMER(tt_2msl);
#undef COPYTIMER
		xt->t_rcvtime = 1000 * (ticks - tp->t_rcvtime) / hz;

		bcopy(tp->t_fb->tfb_tcp_block_name, xt->xt_stack,
		    TCP_FUNCTION_NAME_LEN_MAX);
#ifdef TCP_BLACKBOX
		(void)tcp_log_get_id(tp, xt->xt_logid);
#endif
	}

	xt->xt_len = sizeof(struct xtcpcb);
	in_pcbtoxinpcb(inp, &xt->xt_inp);
	if (inp->inp_socket == NULL)
		xt->xt_inp.xi_socket.xso_protocol = IPPROTO_TCP;
}
