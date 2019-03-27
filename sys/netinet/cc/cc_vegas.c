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
 * An implementation of the Vegas congestion control algorithm for FreeBSD,
 * based on L. S. Brakmo and L. L. Peterson, "TCP Vegas: end to end congestion
 * avoidance on a global internet", IEEE J. Sel. Areas Commun., vol. 13, no. 8,
 * pp. 1465-1480, Oct. 1995. The original Vegas duplicate ack policy has not
 * been implemented, since clock ticks are not as coarse as they were (i.e.
 * 500ms) when Vegas was designed. Also, packets are timed once per RTT as in
 * the original paper.
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
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <net/vnet.h>

#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/cc/cc.h>
#include <netinet/cc/cc_module.h>

#include <netinet/khelp/h_ertt.h>

/*
 * Private signal type for rate based congestion signal.
 * See <netinet/cc.h> for appropriate bit-range to use for private signals.
 */
#define	CC_VEGAS_RATE	0x01000000

static void	vegas_ack_received(struct cc_var *ccv, uint16_t ack_type);
static void	vegas_cb_destroy(struct cc_var *ccv);
static int	vegas_cb_init(struct cc_var *ccv);
static void	vegas_cong_signal(struct cc_var *ccv, uint32_t signal_type);
static void	vegas_conn_init(struct cc_var *ccv);
static int	vegas_mod_init(void);

struct vegas {
	int slow_start_toggle;
};

static int32_t ertt_id;

VNET_DEFINE_STATIC(uint32_t, vegas_alpha) = 1;
VNET_DEFINE_STATIC(uint32_t, vegas_beta) = 3;
#define	V_vegas_alpha	VNET(vegas_alpha)
#define	V_vegas_beta	VNET(vegas_beta)

static MALLOC_DEFINE(M_VEGAS, "vegas data",
    "Per connection data required for the Vegas congestion control algorithm");

struct cc_algo vegas_cc_algo = {
	.name = "vegas",
	.ack_received = vegas_ack_received,
	.cb_destroy = vegas_cb_destroy,
	.cb_init = vegas_cb_init,
	.cong_signal = vegas_cong_signal,
	.conn_init = vegas_conn_init,
	.mod_init = vegas_mod_init
};

/*
 * The vegas window adjustment is done once every RTT, as indicated by the
 * ERTT_NEW_MEASUREMENT flag. This flag is reset once the new measurment data
 * has been used.
 */
static void
vegas_ack_received(struct cc_var *ccv, uint16_t ack_type)
{
	struct ertt *e_t;
	struct vegas *vegas_data;
	long actual_tx_rate, expected_tx_rate, ndiff;

	e_t = khelp_get_osd(CCV(ccv, osd), ertt_id);
	vegas_data = ccv->cc_data;

	if (e_t->flags & ERTT_NEW_MEASUREMENT) { /* Once per RTT. */
		if (e_t->minrtt && e_t->markedpkt_rtt) {
			expected_tx_rate = e_t->marked_snd_cwnd / e_t->minrtt;
			actual_tx_rate = e_t->bytes_tx_in_marked_rtt /
			    e_t->markedpkt_rtt;
			ndiff = (expected_tx_rate - actual_tx_rate) *
			    e_t->minrtt / CCV(ccv, t_maxseg);

			if (ndiff < V_vegas_alpha) {
				if (CCV(ccv, snd_cwnd) <=
				    CCV(ccv, snd_ssthresh)) {
					vegas_data->slow_start_toggle =
					    vegas_data->slow_start_toggle ?
					    0 : 1;
				} else {
					vegas_data->slow_start_toggle = 0;
					CCV(ccv, snd_cwnd) =
					    min(CCV(ccv, snd_cwnd) +
					    CCV(ccv, t_maxseg),
					    TCP_MAXWIN << CCV(ccv, snd_scale));
				}
			} else if (ndiff > V_vegas_beta) {
				/* Rate-based congestion. */
				vegas_cong_signal(ccv, CC_VEGAS_RATE);
				vegas_data->slow_start_toggle = 0;
			}
		}
		e_t->flags &= ~ERTT_NEW_MEASUREMENT;
	}

	if (vegas_data->slow_start_toggle)
		newreno_cc_algo.ack_received(ccv, ack_type);
}

static void
vegas_cb_destroy(struct cc_var *ccv)
{
	free(ccv->cc_data, M_VEGAS);
}

static int
vegas_cb_init(struct cc_var *ccv)
{
	struct vegas *vegas_data;

	vegas_data = malloc(sizeof(struct vegas), M_VEGAS, M_NOWAIT);

	if (vegas_data == NULL)
		return (ENOMEM);

	vegas_data->slow_start_toggle = 1;
	ccv->cc_data = vegas_data;

	return (0);
}

/*
 * If congestion has been triggered triggered by the Vegas measured rates, it is
 * handled here, otherwise it falls back to newreno's congestion handling.
 */
static void
vegas_cong_signal(struct cc_var *ccv, uint32_t signal_type)
{
	struct vegas *vegas_data;
	int presignalrecov;

	vegas_data = ccv->cc_data;

	if (IN_RECOVERY(CCV(ccv, t_flags)))
		presignalrecov = 1;
	else
		presignalrecov = 0;

	switch(signal_type) {
	case CC_VEGAS_RATE:
		if (!IN_RECOVERY(CCV(ccv, t_flags))) {
			CCV(ccv, snd_cwnd) = max(2 * CCV(ccv, t_maxseg),
			    CCV(ccv, snd_cwnd) - CCV(ccv, t_maxseg));
			if (CCV(ccv, snd_cwnd) < CCV(ccv, snd_ssthresh))
				/* Exit slow start. */
				CCV(ccv, snd_ssthresh) = CCV(ccv, snd_cwnd);
		}
		break;

	default:
		newreno_cc_algo.cong_signal(ccv, signal_type);
	}

	if (IN_RECOVERY(CCV(ccv, t_flags)) && !presignalrecov)
		vegas_data->slow_start_toggle =
		    (CCV(ccv, snd_cwnd) < CCV(ccv, snd_ssthresh)) ? 1 : 0;
}

static void
vegas_conn_init(struct cc_var *ccv)
{
	struct vegas *vegas_data;

	vegas_data = ccv->cc_data;
	vegas_data->slow_start_toggle = 1;
}

static int
vegas_mod_init(void)
{

	ertt_id = khelp_get_id("ertt");
	if (ertt_id <= 0) {
		printf("%s: h_ertt module not found\n", __func__);
		return (ENOENT);
	}

	vegas_cc_algo.after_idle = newreno_cc_algo.after_idle;
	vegas_cc_algo.post_recovery = newreno_cc_algo.post_recovery;

	return (0);
}

static int
vegas_alpha_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_vegas_alpha;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new > V_vegas_beta)
			error = EINVAL;
		else
			V_vegas_alpha = new;
	}

	return (error);
}

static int
vegas_beta_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	uint32_t new;

	new = V_vegas_beta;
	error = sysctl_handle_int(oidp, &new, 0, req);
	if (error == 0 && req->newptr != NULL) {
		if (new == 0 || new < V_vegas_alpha)
			 error = EINVAL;
		else
			V_vegas_beta = new;
	}

	return (error);
}

SYSCTL_DECL(_net_inet_tcp_cc_vegas);
SYSCTL_NODE(_net_inet_tcp_cc, OID_AUTO, vegas, CTLFLAG_RW, NULL,
    "Vegas related settings");

SYSCTL_PROC(_net_inet_tcp_cc_vegas, OID_AUTO, alpha,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(vegas_alpha), 1, &vegas_alpha_handler, "IU",
    "vegas alpha, specified as number of \"buffers\" (0 < alpha < beta)");

SYSCTL_PROC(_net_inet_tcp_cc_vegas, OID_AUTO, beta,
    CTLFLAG_VNET | CTLTYPE_UINT | CTLFLAG_RW,
    &VNET_NAME(vegas_beta), 3, &vegas_beta_handler, "IU",
    "vegas beta, specified as number of \"buffers\" (0 < alpha < beta)");

DECLARE_CC_MODULE(vegas, &vegas_cc_algo);
MODULE_DEPEND(vegas, ertt, 1, 1, 1);
