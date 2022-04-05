/******************************************************************************

(c) 2008 NetApp.  All Rights Reserved.

NetApp provides this source code under the GPL v2 License.
The GPL v2 license is available at
http://opensource.org/licenses/gpl-license.php.

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

/*
 * Functions to create and manage the backchannel
 */

#ifndef _LINUX_SUNRPC_BC_XPRT_H
#define _LINUX_SUNRPC_BC_XPRT_H

#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>

#ifdef CONFIG_SUNRPC_BACKCHANNEL
struct rpc_rqst *xprt_lookup_bc_request(struct rpc_xprt *xprt, __be32 xid);
void xprt_complete_bc_request(struct rpc_rqst *req, uint32_t copied);
void xprt_init_bc_request(struct rpc_rqst *req, struct rpc_task *task);
void xprt_free_bc_request(struct rpc_rqst *req);
int xprt_setup_backchannel(struct rpc_xprt *, unsigned int min_reqs);
void xprt_destroy_backchannel(struct rpc_xprt *, unsigned int max_reqs);

/* Socket backchannel transport methods */
int xprt_setup_bc(struct rpc_xprt *xprt, unsigned int min_reqs);
void xprt_destroy_bc(struct rpc_xprt *xprt, unsigned int max_reqs);
void xprt_free_bc_rqst(struct rpc_rqst *req);
unsigned int xprt_bc_max_slots(struct rpc_xprt *xprt);

/*
 * Determine if a shared backchannel is in use
 */
static inline bool svc_is_backchannel(const struct svc_rqst *rqstp)
{
	return rqstp->rq_server->sv_bc_enabled;
}

static inline void set_bc_enabled(struct svc_serv *serv)
{
	serv->sv_bc_enabled = true;
}
#else /* CONFIG_SUNRPC_BACKCHANNEL */
static inline int xprt_setup_backchannel(struct rpc_xprt *xprt,
					 unsigned int min_reqs)
{
	return 0;
}

static inline void xprt_destroy_backchannel(struct rpc_xprt *xprt,
					    unsigned int max_reqs)
{
}

static inline bool svc_is_backchannel(const struct svc_rqst *rqstp)
{
	return false;
}

static inline void set_bc_enabled(struct svc_serv *serv)
{
}

static inline void xprt_free_bc_request(struct rpc_rqst *req)
{
}
#endif /* CONFIG_SUNRPC_BACKCHANNEL */
#endif /* _LINUX_SUNRPC_BC_XPRT_H */

