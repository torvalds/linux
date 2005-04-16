/*
 * linux/net/sunrpc/timer.c
 *
 * Estimate RPC request round trip time.
 *
 * Based on packet round-trip and variance estimator algorithms described
 * in appendix A of "Congestion Avoidance and Control" by Van Jacobson
 * and Michael J. Karels (ACM Computer Communication Review; Proceedings
 * of the Sigcomm '88 Symposium in Stanford, CA, August, 1988).
 *
 * This RTT estimator is used only for RPC over datagram protocols.
 *
 * Copyright (C) 2002 Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#include <asm/param.h>

#include <linux/types.h>
#include <linux/unistd.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/timer.h>

#define RPC_RTO_MAX (60*HZ)
#define RPC_RTO_INIT (HZ/5)
#define RPC_RTO_MIN (HZ/10)

void
rpc_init_rtt(struct rpc_rtt *rt, unsigned long timeo)
{
	unsigned long init = 0;
	unsigned i;

	rt->timeo = timeo;

	if (timeo > RPC_RTO_INIT)
		init = (timeo - RPC_RTO_INIT) << 3;
	for (i = 0; i < 5; i++) {
		rt->srtt[i] = init;
		rt->sdrtt[i] = RPC_RTO_INIT;
		rt->ntimeouts[i] = 0;
	}
}

/*
 * NB: When computing the smoothed RTT and standard deviation,
 *     be careful not to produce negative intermediate results.
 */
void
rpc_update_rtt(struct rpc_rtt *rt, unsigned timer, long m)
{
	long *srtt, *sdrtt;

	if (timer-- == 0)
		return;

	/* jiffies wrapped; ignore this one */
	if (m < 0)
		return;

	if (m == 0)
		m = 1L;

	srtt = (long *)&rt->srtt[timer];
	m -= *srtt >> 3;
	*srtt += m;

	if (m < 0)
		m = -m;

	sdrtt = (long *)&rt->sdrtt[timer];
	m -= *sdrtt >> 2;
	*sdrtt += m;

	/* Set lower bound on the variance */
	if (*sdrtt < RPC_RTO_MIN)
		*sdrtt = RPC_RTO_MIN;
}

/*
 * Estimate rto for an nfs rpc sent via. an unreliable datagram.
 * Use the mean and mean deviation of rtt for the appropriate type of rpc
 * for the frequent rpcs and a default for the others.
 * The justification for doing "other" this way is that these rpcs
 * happen so infrequently that timer est. would probably be stale.
 * Also, since many of these rpcs are
 * non-idempotent, a conservative timeout is desired.
 * getattr, lookup,
 * read, write, commit     - A+4D
 * other                   - timeo
 */

unsigned long
rpc_calc_rto(struct rpc_rtt *rt, unsigned timer)
{
	unsigned long res;

	if (timer-- == 0)
		return rt->timeo;

	res = ((rt->srtt[timer] + 7) >> 3) + rt->sdrtt[timer];
	if (res > RPC_RTO_MAX)
		res = RPC_RTO_MAX;

	return res;
}
