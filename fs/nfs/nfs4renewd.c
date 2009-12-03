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
 * TODO: If the send queue gets backlogged (e.g., if the server goes down),
 * we will keep filling the queue with periodic RENEW requests.  We need a
 * mechanism for ensuring that if renewd successfully sends off a request,
 * then it only wakes up when the request is finished.  Maybe use the
 * child task framework of the RPC layer?
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

#define NFSDBG_FACILITY	NFSDBG_PROC

void
nfs4_renew_state(struct work_struct *work)
{
	struct nfs4_state_maintenance_ops *ops;
	struct nfs_client *clp =
		container_of(work, struct nfs_client, cl_renewd.work);
	struct rpc_cred *cred;
	long lease, timeout;
	unsigned long last, now;

	ops = nfs4_state_renewal_ops[clp->cl_minorversion];
	dprintk("%s: start\n", __func__);
	/* Are there any active superblocks? */
	if (list_empty(&clp->cl_superblocks))
		goto out;
	spin_lock(&clp->cl_lock);
	lease = clp->cl_lease_time;
	last = clp->cl_last_renewal;
	now = jiffies;
	timeout = (2 * lease) / 3 + (long)last - (long)now;
	/* Are we close to a lease timeout? */
	if (time_after(now, last + lease/3)) {
		cred = ops->get_state_renewal_cred_locked(clp);
		spin_unlock(&clp->cl_lock);
		if (cred == NULL) {
			if (list_empty(&clp->cl_delegations)) {
				set_bit(NFS4CLNT_LEASE_EXPIRED, &clp->cl_state);
				goto out;
			}
			nfs_expire_all_delegations(clp);
		} else {
			/* Queue an asynchronous RENEW. */
			ops->sched_state_renewal(clp, cred);
			put_rpccred(cred);
		}
		timeout = (2 * lease) / 3;
		spin_lock(&clp->cl_lock);
	} else
		dprintk("%s: failed to call renewd. Reason: lease not expired \n",
				__func__);
	if (timeout < 5 * HZ)    /* safeguard */
		timeout = 5 * HZ;
	dprintk("%s: requeueing work. Lease period = %ld\n",
			__func__, (timeout + HZ - 1) / HZ);
	cancel_delayed_work(&clp->cl_renewd);
	schedule_delayed_work(&clp->cl_renewd, timeout);
	spin_unlock(&clp->cl_lock);
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
	cancel_delayed_work(&clp->cl_renewd);
	schedule_delayed_work(&clp->cl_renewd, timeout);
	set_bit(NFS_CS_RENEWD, &clp->cl_res_state);
	spin_unlock(&clp->cl_lock);
}

void
nfs4_kill_renewd(struct nfs_client *clp)
{
	cancel_delayed_work_sync(&clp->cl_renewd);
}

/*
 * Local variables:
 *   c-basic-offset: 8
 * End:
 */
