/******************************************************************************

(c) 2007 Network Appliance, Inc.  All Rights Reserved.
(c) 2009 NetApp.  All Rights Reserved.

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
 * The NFSv4.1 callback service helper routines.
 * They implement the transport level processing required to send the
 * reply over an existing open connection previously established by the client.
 */

#if defined(CONFIG_NFS_V4_1)

#include <linux/module.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/bc_xprt.h>

#define RPCDBG_FACILITY	RPCDBG_SVCDSP

/* Empty callback ops */
static const struct rpc_call_ops nfs41_callback_ops = {
};


/*
 * Send the callback reply
 */
int bc_send(struct rpc_rqst *req)
{
	struct rpc_task *task;
	int ret;

	dprintk("RPC:       bc_send req= %p\n", req);
	task = rpc_run_bc_task(req, &nfs41_callback_ops);
	if (IS_ERR(task))
		ret = PTR_ERR(task);
	else {
		BUG_ON(atomic_read(&task->tk_count) != 1);
		ret = task->tk_status;
		rpc_put_task(task);
	}
	dprintk("RPC:       bc_send ret= %d\n", ret);
	return ret;
}

#endif /* CONFIG_NFS_V4_1 */
