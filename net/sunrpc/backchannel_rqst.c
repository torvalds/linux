/******************************************************************************

(c) 2007 Network Appliance, Inc.  All Rights Reserved.
(c) 2009 NetApp.  All Rights Reserved.

NetApp provides this source code under the GPL v2 License.
The GPL v2 license is available at
https://opensource.org/licenses/gpl-license.php.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

#include <linux/tcp.h>
#include <linux/slab.h>
#include <linux/sunrpc/xprt.h>
#include <linux/export.h>
#include <linux/sunrpc/bc_xprt.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
#define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

#define BC_MAX_SLOTS	64U

unsigned int xprt_bc_max_slots(struct rpc_xprt *xprt)
{
	return BC_MAX_SLOTS;
}

/*
 * Helper routines that track the number of preallocation elements
 * on the transport.
 */
static inline int xprt_need_to_requeue(struct rpc_xprt *xprt)
{
	return xprt->bc_alloc_count < xprt->bc_alloc_max;
}

/*
 * Free the preallocated rpc_rqst structure and the memory
 * buffers hanging off of it.
 */
static void xprt_free_allocation(struct rpc_rqst *req)
{
	struct xdr_buf *xbufp;

	dprintk("RPC:        free allocations for req= %p\n", req);
	WARN_ON_ONCE(test_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state));
	xbufp = &req->rq_rcv_buf;
	free_page((unsigned long)xbufp->head[0].iov_base);
	xbufp = &req->rq_snd_buf;
	free_page((unsigned long)xbufp->head[0].iov_base);
	kfree(req);
}

static int xprt_alloc_xdr_buf(struct xdr_buf *buf, gfp_t gfp_flags)
{
	struct page *page;
	/* Preallocate one XDR receive buffer */
	page = alloc_page(gfp_flags);
	if (page == NULL)
		return -ENOMEM;
	xdr_buf_init(buf, page_address(page), PAGE_SIZE);
	return 0;
}

static
struct rpc_rqst *xprt_alloc_bc_req(struct rpc_xprt *xprt, gfp_t gfp_flags)
{
	struct rpc_rqst *req;

	/* Pre-allocate one backchannel rpc_rqst */
	req = kzalloc(sizeof(*req), gfp_flags);
	if (req == NULL)
		return NULL;

	req->rq_xprt = xprt;
	INIT_LIST_HEAD(&req->rq_bc_list);

	/* Preallocate one XDR receive buffer */
	if (xprt_alloc_xdr_buf(&req->rq_rcv_buf, gfp_flags) < 0) {
		printk(KERN_ERR "Failed to create bc receive xbuf\n");
		goto out_free;
	}
	req->rq_rcv_buf.len = PAGE_SIZE;

	/* Preallocate one XDR send buffer */
	if (xprt_alloc_xdr_buf(&req->rq_snd_buf, gfp_flags) < 0) {
		printk(KERN_ERR "Failed to create bc snd xbuf\n");
		goto out_free;
	}
	return req;
out_free:
	xprt_free_allocation(req);
	return NULL;
}

/*
 * Preallocate up to min_reqs structures and related buffers for use
 * by the backchannel.  This function can be called multiple times
 * when creating new sessions that use the same rpc_xprt.  The
 * preallocated buffers are added to the pool of resources used by
 * the rpc_xprt.  Anyone of these resources may be used used by an
 * incoming callback request.  It's up to the higher levels in the
 * stack to enforce that the maximum number of session slots is not
 * being exceeded.
 *
 * Some callback arguments can be large.  For example, a pNFS server
 * using multiple deviceids.  The list can be unbound, but the client
 * has the ability to tell the server the maximum size of the callback
 * requests.  Each deviceID is 16 bytes, so allocate one page
 * for the arguments to have enough room to receive a number of these
 * deviceIDs.  The NFS client indicates to the pNFS server that its
 * callback requests can be up to 4096 bytes in size.
 */
int xprt_setup_backchannel(struct rpc_xprt *xprt, unsigned int min_reqs)
{
	if (!xprt->ops->bc_setup)
		return 0;
	return xprt->ops->bc_setup(xprt, min_reqs);
}
EXPORT_SYMBOL_GPL(xprt_setup_backchannel);

int xprt_setup_bc(struct rpc_xprt *xprt, unsigned int min_reqs)
{
	struct rpc_rqst *req;
	struct list_head tmp_list;
	int i;

	dprintk("RPC:       setup backchannel transport\n");

	if (min_reqs > BC_MAX_SLOTS)
		min_reqs = BC_MAX_SLOTS;

	/*
	 * We use a temporary list to keep track of the preallocated
	 * buffers.  Once we're done building the list we splice it
	 * into the backchannel preallocation list off of the rpc_xprt
	 * struct.  This helps minimize the amount of time the list
	 * lock is held on the rpc_xprt struct.  It also makes cleanup
	 * easier in case of memory allocation errors.
	 */
	INIT_LIST_HEAD(&tmp_list);
	for (i = 0; i < min_reqs; i++) {
		/* Pre-allocate one backchannel rpc_rqst */
		req = xprt_alloc_bc_req(xprt, GFP_KERNEL);
		if (req == NULL) {
			printk(KERN_ERR "Failed to create bc rpc_rqst\n");
			goto out_free;
		}

		/* Add the allocated buffer to the tmp list */
		dprintk("RPC:       adding req= %p\n", req);
		list_add(&req->rq_bc_pa_list, &tmp_list);
	}

	/*
	 * Add the temporary list to the backchannel preallocation list
	 */
	spin_lock(&xprt->bc_pa_lock);
	list_splice(&tmp_list, &xprt->bc_pa_list);
	xprt->bc_alloc_count += min_reqs;
	xprt->bc_alloc_max += min_reqs;
	atomic_add(min_reqs, &xprt->bc_slot_count);
	spin_unlock(&xprt->bc_pa_lock);

	dprintk("RPC:       setup backchannel transport done\n");
	return 0;

out_free:
	/*
	 * Memory allocation failed, free the temporary list
	 */
	while (!list_empty(&tmp_list)) {
		req = list_first_entry(&tmp_list,
				struct rpc_rqst,
				rq_bc_pa_list);
		list_del(&req->rq_bc_pa_list);
		xprt_free_allocation(req);
	}

	dprintk("RPC:       setup backchannel transport failed\n");
	return -ENOMEM;
}

/**
 * xprt_destroy_backchannel - Destroys the backchannel preallocated structures.
 * @xprt:	the transport holding the preallocated strucures
 * @max_reqs:	the maximum number of preallocated structures to destroy
 *
 * Since these structures may have been allocated by multiple calls
 * to xprt_setup_backchannel, we only destroy up to the maximum number
 * of reqs specified by the caller.
 */
void xprt_destroy_backchannel(struct rpc_xprt *xprt, unsigned int max_reqs)
{
	if (xprt->ops->bc_destroy)
		xprt->ops->bc_destroy(xprt, max_reqs);
}
EXPORT_SYMBOL_GPL(xprt_destroy_backchannel);

void xprt_destroy_bc(struct rpc_xprt *xprt, unsigned int max_reqs)
{
	struct rpc_rqst *req = NULL, *tmp = NULL;

	dprintk("RPC:        destroy backchannel transport\n");

	if (max_reqs == 0)
		goto out;

	spin_lock_bh(&xprt->bc_pa_lock);
	xprt->bc_alloc_max -= min(max_reqs, xprt->bc_alloc_max);
	list_for_each_entry_safe(req, tmp, &xprt->bc_pa_list, rq_bc_pa_list) {
		dprintk("RPC:        req=%p\n", req);
		list_del(&req->rq_bc_pa_list);
		xprt_free_allocation(req);
		xprt->bc_alloc_count--;
		atomic_dec(&xprt->bc_slot_count);
		if (--max_reqs == 0)
			break;
	}
	spin_unlock_bh(&xprt->bc_pa_lock);

out:
	dprintk("RPC:        backchannel list empty= %s\n",
		list_empty(&xprt->bc_pa_list) ? "true" : "false");
}

static struct rpc_rqst *xprt_get_bc_request(struct rpc_xprt *xprt, __be32 xid,
		struct rpc_rqst *new)
{
	struct rpc_rqst *req = NULL;

	dprintk("RPC:       allocate a backchannel request\n");
	if (list_empty(&xprt->bc_pa_list)) {
		if (!new)
			goto not_found;
		if (atomic_read(&xprt->bc_slot_count) >= BC_MAX_SLOTS)
			goto not_found;
		list_add_tail(&new->rq_bc_pa_list, &xprt->bc_pa_list);
		xprt->bc_alloc_count++;
		atomic_inc(&xprt->bc_slot_count);
	}
	req = list_first_entry(&xprt->bc_pa_list, struct rpc_rqst,
				rq_bc_pa_list);
	req->rq_reply_bytes_recvd = 0;
	memcpy(&req->rq_private_buf, &req->rq_rcv_buf,
			sizeof(req->rq_private_buf));
	req->rq_xid = xid;
	req->rq_connect_cookie = xprt->connect_cookie;
	dprintk("RPC:       backchannel req=%p\n", req);
not_found:
	return req;
}

/*
 * Return the preallocated rpc_rqst structure and XDR buffers
 * associated with this rpc_task.
 */
void xprt_free_bc_request(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;

	xprt->ops->bc_free_rqst(req);
}

void xprt_free_bc_rqst(struct rpc_rqst *req)
{
	struct rpc_xprt *xprt = req->rq_xprt;

	dprintk("RPC:       free backchannel req=%p\n", req);

	req->rq_connect_cookie = xprt->connect_cookie - 1;
	smp_mb__before_atomic();
	clear_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state);
	smp_mb__after_atomic();

	/*
	 * Return it to the list of preallocations so that it
	 * may be reused by a new callback request.
	 */
	spin_lock_bh(&xprt->bc_pa_lock);
	if (xprt_need_to_requeue(xprt)) {
		list_add_tail(&req->rq_bc_pa_list, &xprt->bc_pa_list);
		xprt->bc_alloc_count++;
		atomic_inc(&xprt->bc_slot_count);
		req = NULL;
	}
	spin_unlock_bh(&xprt->bc_pa_lock);
	if (req != NULL) {
		/*
		 * The last remaining session was destroyed while this
		 * entry was in use.  Free the entry and don't attempt
		 * to add back to the list because there is no need to
		 * have anymore preallocated entries.
		 */
		dprintk("RPC:       Last session removed req=%p\n", req);
		xprt_free_allocation(req);
	}
	xprt_put(xprt);
}

/*
 * One or more rpc_rqst structure have been preallocated during the
 * backchannel setup.  Buffer space for the send and private XDR buffers
 * has been preallocated as well.  Use xprt_alloc_bc_request to allocate
 * to this request.  Use xprt_free_bc_request to return it.
 *
 * We know that we're called in soft interrupt context, grab the spin_lock
 * since there is no need to grab the bottom half spin_lock.
 *
 * Return an available rpc_rqst, otherwise NULL if non are available.
 */
struct rpc_rqst *xprt_lookup_bc_request(struct rpc_xprt *xprt, __be32 xid)
{
	struct rpc_rqst *req, *new = NULL;

	do {
		spin_lock(&xprt->bc_pa_lock);
		list_for_each_entry(req, &xprt->bc_pa_list, rq_bc_pa_list) {
			if (req->rq_connect_cookie != xprt->connect_cookie)
				continue;
			if (req->rq_xid == xid)
				goto found;
		}
		req = xprt_get_bc_request(xprt, xid, new);
found:
		spin_unlock(&xprt->bc_pa_lock);
		if (new) {
			if (req != new)
				xprt_free_allocation(new);
			break;
		} else if (req)
			break;
		new = xprt_alloc_bc_req(xprt, GFP_KERNEL);
	} while (new);
	return req;
}

/*
 * Add callback request to callback list.  The callback
 * service sleeps on the sv_cb_waitq waiting for new
 * requests.  Wake it up after adding enqueing the
 * request.
 */
void xprt_complete_bc_request(struct rpc_rqst *req, uint32_t copied)
{
	struct rpc_xprt *xprt = req->rq_xprt;
	struct svc_serv *bc_serv = xprt->bc_serv;

	spin_lock(&xprt->bc_pa_lock);
	list_del(&req->rq_bc_pa_list);
	xprt->bc_alloc_count--;
	spin_unlock(&xprt->bc_pa_lock);

	req->rq_private_buf.len = copied;
	set_bit(RPC_BC_PA_IN_USE, &req->rq_bc_pa_state);

	dprintk("RPC:       add callback request to list\n");
	xprt_get(xprt);
	spin_lock(&bc_serv->sv_cb_lock);
	list_add(&req->rq_bc_list, &bc_serv->sv_cb_list);
	wake_up(&bc_serv->sv_cb_waitq);
	spin_unlock(&bc_serv->sv_cb_lock);
}
