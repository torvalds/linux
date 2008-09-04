/*
 * dccp_probe - Observe the DCCP flow with kprobes.
 *
 * The idea for this came from Werner Almesberger's umlsim
 * Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
 *
 * Modified for DCCP from Stephen Hemminger's code
 * Copyright (C) 2006, Ian McDonald <ian.mcdonald@jandi.co.nz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/socket.h>
#include <linux/dccp.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/vmalloc.h>
#include <net/net_namespace.h>

#include "dccp.h"
#include "ccid.h"
#include "ccids/ccid3.h"

static int port;

static int bufsize = 64 * 1024;

static const char procname[] = "dccpprobe";

static struct {
	struct kfifo	  *fifo;
	spinlock_t	  lock;
	wait_queue_head_t wait;
	struct timespec	  tstart;
} dccpw;

static void printl(const char *fmt, ...)
{
	va_list args;
	int len;
	struct timespec now;
	char tbuf[256];

	va_start(args, fmt);
	getnstimeofday(&now);

	now = timespec_sub(now, dccpw.tstart);

	len = sprintf(tbuf, "%lu.%06lu ",
		      (unsigned long) now.tv_sec,
		      (unsigned long) now.tv_nsec / NSEC_PER_USEC);
	len += vscnprintf(tbuf+len, sizeof(tbuf)-len, fmt, args);
	va_end(args);

	kfifo_put(dccpw.fifo, tbuf, len);
	wake_up(&dccpw.wait);
}

static int jdccp_sendmsg(struct kiocb *iocb, struct sock *sk,
			 struct msghdr *msg, size_t size)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct ccid3_hc_tx_sock *hctx = NULL;

	if (ccid_get_current_tx_ccid(dccp_sk(sk)) == DCCPC_CCID3)
		hctx = ccid3_hc_tx_sk(sk);

	if (port == 0 || ntohs(inet->dport) == port ||
	    ntohs(inet->sport) == port) {
		if (hctx)
			printl("%d.%d.%d.%d:%u %d.%d.%d.%d:%u %d %d %d %d %u "
			       "%llu %llu %d\n",
			       NIPQUAD(inet->saddr), ntohs(inet->sport),
			       NIPQUAD(inet->daddr), ntohs(inet->dport), size,
			       hctx->ccid3hctx_s, hctx->ccid3hctx_rtt,
			       hctx->ccid3hctx_p, hctx->ccid3hctx_x_calc,
			       hctx->ccid3hctx_x_recv >> 6,
			       hctx->ccid3hctx_x >> 6, hctx->ccid3hctx_t_ipi);
		else
			printl("%d.%d.%d.%d:%u %d.%d.%d.%d:%u %d\n",
			       NIPQUAD(inet->saddr), ntohs(inet->sport),
			       NIPQUAD(inet->daddr), ntohs(inet->dport), size);
	}

	jprobe_return();
	return 0;
}

static struct jprobe dccp_send_probe = {
	.kp	= {
		.symbol_name = "dccp_sendmsg",
	},
	.entry	= jdccp_sendmsg,
};

static int dccpprobe_open(struct inode *inode, struct file *file)
{
	kfifo_reset(dccpw.fifo);
	getnstimeofday(&dccpw.tstart);
	return 0;
}

static ssize_t dccpprobe_read(struct file *file, char __user *buf,
			      size_t len, loff_t *ppos)
{
	int error = 0, cnt = 0;
	unsigned char *tbuf;

	if (!buf)
		return -EINVAL;

	if (len == 0)
		return 0;

	tbuf = vmalloc(len);
	if (!tbuf)
		return -ENOMEM;

	error = wait_event_interruptible(dccpw.wait,
					 __kfifo_len(dccpw.fifo) != 0);
	if (error)
		goto out_free;

	cnt = kfifo_get(dccpw.fifo, tbuf, len);
	error = copy_to_user(buf, tbuf, cnt) ? -EFAULT : 0;

out_free:
	vfree(tbuf);

	return error ? error : cnt;
}

static const struct file_operations dccpprobe_fops = {
	.owner	 = THIS_MODULE,
	.open	 = dccpprobe_open,
	.read    = dccpprobe_read,
};

static __init int dccpprobe_init(void)
{
	int ret = -ENOMEM;

	init_waitqueue_head(&dccpw.wait);
	spin_lock_init(&dccpw.lock);
	dccpw.fifo = kfifo_alloc(bufsize, GFP_KERNEL, &dccpw.lock);
	if (IS_ERR(dccpw.fifo))
		return PTR_ERR(dccpw.fifo);

	if (!proc_net_fops_create(&init_net, procname, S_IRUSR, &dccpprobe_fops))
		goto err0;

	ret = register_jprobe(&dccp_send_probe);
	if (ret)
		goto err1;

	pr_info("DCCP watch registered (port=%d)\n", port);
	return 0;
err1:
	proc_net_remove(&init_net, procname);
err0:
	kfifo_free(dccpw.fifo);
	return ret;
}
module_init(dccpprobe_init);

static __exit void dccpprobe_exit(void)
{
	kfifo_free(dccpw.fifo);
	proc_net_remove(&init_net, procname);
	unregister_jprobe(&dccp_send_probe);

}
module_exit(dccpprobe_exit);

MODULE_PARM_DESC(port, "Port to match (0=all)");
module_param(port, int, 0);

MODULE_PARM_DESC(bufsize, "Log buffer size (default 64k)");
module_param(bufsize, int, 0);

MODULE_AUTHOR("Ian McDonald <ian.mcdonald@jandi.co.nz>");
MODULE_DESCRIPTION("DCCP snooper");
MODULE_LICENSE("GPL");
