/*
 *  fs/nfs/nfs4renewd.c
 *
 *  Copyright (c) 2002 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Implementation of the NFSv4 "renew daemon", which wakes up periodically to
 * send a RENEW, to keep state alive on the server.  The daemon is implemented
 * as an rpc_task, not a real kernel thread, so it always runs in rpciod's
 * context.  There is one renewd per nfs_server.
 *
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>

#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "delegation.h"

#define NFSDBG_FACILITY		NFSDBG_STATE

void
nfs4_renew_state(struct work_struct *work)
{
	const struct nfs4_state_maintenance_ops *ops;
	struct nfs_client *clp =
		container_of(work, struct nfs_client, cl_renewd.work);
	const struct cred *cred;
	long lease;
	unsigned long last, now;
	unsigned renew_flags = 0;

	ops = clp->cl_mvops->state_renewal_ops;
	dprintk("%s: start\n", __func__);

	if (test_bit(NFS_CS_STOP_RENEW, &clp->cl_res_state))
		goto out;

	lease = clp->cl_lease_time;
	last = clp->cl_last_renewal;
	now = jiffies;
	/* Are we close to a lease timeout? */
	if (time_after(now, last + lease/3))
		renew_flags |= NFS4_RENEW_TIMEOUT;
	if (nfs_delegations_present(clp))
		renew_flags |= NFS4_RENEW_DELEGATION_CB;

	if (renew_flags != 0) {
		cred = ops->get_state_renewal_cred(clp);
		if (cred == NULL) {
			if (!(renew_flags & NFS4_RENEW_DELEGATION_CB)) {
				set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
				goto out;
			}
			nfs_expire_all_delegations(clp);
		} else {
			int ret;

			/* Queue an asynchronous RENEW. */
			ret = ops->sched_state_renewal(clp, cred, renew_flags);
			put_cred(cred);
			switch (ret) {
			default:
				goto out_exp;
			case -EAGAIN:
			case -ENOMEM:
				break;
			}
		}
	} else {
		dprintk("%s: failed to call renewd. Reason: lease not expired \n",
				__func__);
	}
	nfs4_schedule_state_renewal(clp);
out_exp:
	nfs_expire_unreferenced_delegations(clp);
out:
	dprintk("%s: done\n", __func__);
}

void
nfs4_schedule_state_renewal(struct nfs_client *clp)
{
	long timeout;

	spin_lock(&clp->cl_lock);
	timeout = (2 * clp->cl_lease_time) / 3 + (long)clp->cl_last_renewal
		- (long)jiffies;
	if (timeout < 5 * HZ)
		timeout = 5 * HZ;
	dprintk("%s: requeueing work. Lease period = %ld\n",
			__func__, (timeout + HZ - 1) / HZ);
	mod_delayed_work(system_percpu_wq, &clp->cl_renewd, timeout);
	set_bit(NFS_CS_RENEWD, &clp->cl_res_state);
	spin_unlock(&clp->cl_lock);
}

void
nfs4_kill_renewd(struct nfs_client *clp)
{
	cancel_delayed_work_sync(&clp->cl_renewd);
}

/**
 * nfs4_set_lease_period - Sets the lease period on a nfs_client
 *
 * @clp: pointer to nfs_client
 * @lease: new value for lease period
 */
void nfs4_set_lease_period(struct nfs_client *clp,
		unsigned long lease)
{
	spin_lock(&clp->cl_lock);
	clp->cl_lease_time = lease;
	spin_unlock(&clp->cl_lock);

	/* Cap maximum reconnect timeout at 1/2 lease period */
	rpc_set_connect_timeout(clp->cl_rpcclient, lease, lease >> 1);
}
