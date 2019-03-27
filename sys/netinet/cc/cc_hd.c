/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010
 *	Swinburne University of Technology, Melbourne, Australia
 * Copyright (c) 2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010-2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, by David Hayes and
 * Lawrence Stewart, made possible in part by a grant from the Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced Internet
 * Architectures, Swinburne University of Technology, Melbourne, Australia by
 * David Hayes under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * An implementation of the Hamilton Institute's delay-based congestion control
 * algorithm for FreeBSD, based on "A strategy for fair coexistence of loss and
 * delay-based congestion control algorithms," by L. Budzisz, R. Stanojevic, R.
 * Shorten, and F. Baker, IEEE Commun. Lett., vol. 13, no. 7, pp. 555--557, Jul.
 * 2009.
 *
 * Originally released as part of the NewTCP research project at Swinburne
 * University of Technology's Centre for Advanced Internet Architectures,
 * Melbourne, Australia, which was made possible in part by a grant from the
 * Cisco University Research Program Fund at Community Foundation Silicon
 * Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/khelp.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/vnet.h>

#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

/* Largest possible number returned by random(). */
#define	RANDOM_MAX	INT_MAX

static void	hd_ack_received(struct cc_var *ccv, uint16_t ack_type);
static int	hd_mod_init(void);

static int ertt_id;

VNET_DEFINE_STATIC(uint32_t, hd_qthresh) = 20;
VNET_DEFINE_STATIC(uint32_t, hd_qmin) = 5;
VNET_DEFINE_STATIC(uint32_t, hd_pmax) = 5;
#define	V_hd_qthresh	VNET(hd_qthresh)
#define	V_hd_qmin	VNET(hd_qmin)
#define	V_hd_pmax	VNET(hd_pmax)

struct cc_algo hd_cc_algo = {
	.name = "hd",
	.ack_received = hd_ack_received,
	.mod_init = hd_mod_init
};

/*
 * Hamilton backoff function. Returns 1 if we should backoff or 0 otherwise.
 */
static __inline int
should_backoff(int qdly, int maxqdly)
{
	unsigned long p;

	if (qdly < V_hd_qthresh) {
		p = (((RANDOM_MAX / 100) * V_hd_pmax) /
		    (V_hd_qthresh - V_hd_qmin)) * (qdly - V_hd_qmin);
	} else {
		if (qdly > V_hd_qthresh)
			p = (((RANDOM_MAX / 100) * V_hd_pmax) /
			    (maxqdly - V_hd_qthresh)) * (maxqdly - qdly);
		else
			p = (RANDOM_MAX / 100) * V_hd_pmax;
	}

	return (random() < p);
}

/*
 * If the ack type is CC_ACK, and the inferred queueing delay is greater than
 * the Qmin threshold, cwnd is reduced probabilistically. When backing off due
 * to delay, HD behaves like NewReno when an ECN signal is received. HD behaves
 * as NewReno in all other circumstances.
 */
static void
hd_ack_received(struct cc_var *ccv, uint16_t ack_type)
{
	struct ertt *e_t;
	int qdly;

	if (ack_type == CC_ACK) {
		e_t = khelp_get_osd(CCV(ccv, osd), ertt_id);

		if (e_t->rtt && e_t->minrtt && V_hd_qthresh > 0) {
			qdly = e_t->rtt - e_t->minrtt;

			if (qdly > V_hd_qmin &&
			    !IN_RECOVERY(CCV(ccv, t_flags))) {
				/* Probabilistic backoff of cwnd. */
				if (should_backoff(qdly,
				    e_t->maxrtt - e_t->minrtt)) {
					/*
					 * Update cwnd and ssthresh update to
					 * half cwnd and behave like an ECN (ie
					 * not a packet loss).
					 */
					newreno_cc_algo.cong_signal(ccv,
					    CC_ECN);
					return;
				}
			}
		}
	}
	newreno_cc_algo.ack_received(ccv, ack_type); /* As for NewReno. */
}

static int
hd_mod_init(void)
{

	ertt_id = khelp_get_id("ertt");
	if (ertt_id <= 0) {
		printf("%s: h_ertt module not found\n", __func__);
		return (ENOENT);
	}

	hd_cc_algo.after_idle = newreno_cc_algo.after_idle;
	hd_cc_algo.cong_signal = newreno_cc_algo.cong_signal;
	hd_cc_algo.post_recovery = newreno_cc_algo.post_recovery;

	return (0);
}

static int
hd_pmax_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_hd_pmax;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new > 100)
			error = EINVAL;
		else
			V_hd_pmax = new;
	}

	return (error);
}

static int
hd_qmin_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_hd_qmin;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new > V_hd_qthresh)
			error = EINVAL;
		else
			V_hd_qmin = new;
	}

	return (error);
}

static int
hd_qthresh_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_hd_qthresh;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new < V_hd_qmin)
			error = EINVAL;
		else
			V_hd_qthresh = new;
	}

	return (error);
}

SYSCTL_DECL(_net_inet_tcp_cc_hd);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, hd, CTLFLAG_RW, NULL,
    "Hamilton delay-based congestion control related settings");

SYSCTL_PROC(_net_inet_tcp_cc_hd, OID_AUTO, queue_threshold,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, &VNET_NAME(hd_qthresh), 20,
    &hd_qthresh_handler, "IU", "queueing congestion threshold (qth) in ticks");

SYSCTL_PROC(_net_inet_tcp_cc_hd, OID_AUTO, pmax,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, &VNET_NAME(hd_pmax), 5,
    &hd_pmax_handler, "IU",
    "per packet maximum backoff probability as a percentage");

SYSCTL_PROC(_net_inet_tcp_cc_hd, OID_AUTO, queue_min,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW, &VNET_NAME(hd_qmin), 5,
    &hd_qmin_handler, "IU", "minimum queueing delay threshold (qmin) in ticks");

DECLARE_CC_MODULE(hd, &hd_cc_algo);
MODULE_DEPEND(hd, ertt, 1, 1, 1);
