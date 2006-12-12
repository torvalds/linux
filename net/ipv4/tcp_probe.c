/*
 * tcpprobe - Observe the TCP flow with kprobes.
 *
 * The idea for this came from Werner Almesberger's umlsim
 * Copyright (C) 2004, Stephen Hemminger <shemminger@osdl.org>
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
#include <linux/tcp.h>
#include <linux/proc_fs.h>
#include <linux/module.h>
#include <linux/kfifo.h>
#include <linux/vmalloc.h>

#include <net/tcp.h>

MODULE_AUTHOR("Stephen Hemminger <shemminger@osdl.org>");
MODULE_DESCRIPTION("TCP cwnd snooper");
MODULE_LICENSE("GPL");

static int port = 0;
MODULE_PARM_DESC(port, "Port to match (0=all)");
module_param(port, int, 0);

static int bufsize = 64*1024;
MODULE_PARM_DESC(bufsize, "Log buffer size (default 64k)");
module_param(bufsize, int, 0);

static const char procname[] = "tcpprobe";

struct {
	struct kfifo  *fifo;
	spinlock_t    lock;
	wait_queue_head_t wait;
	struct timeval tstart;
} tcpw;

static void printl(const char *fmt, ...)
{
	va_list args;
	int len;
	struct timeval now;
	char tbuf[256];

	va_start(args, fmt);
	do_gettimeofday(&now);

	now.tv_sec -= tcpw.tstart.tv_sec;
	now.tv_usec -= tcpw.tstart.tv_usec;
	if (now.tv_usec < 0) {
		--now.tv_sec;
		now.tv_usec += 1000000;
	}

	len = sprintf(tbuf, "%lu.%06lu ",
		      (unsigned long) now.tv_sec,
		      (unsigned long) now.tv_usec);
	len += vscnprintf(tbuf+len, sizeof(tbuf)-len, fmt, args);
	va_end(args);

	kfifo_put(tcpw.fifo, tbuf, len);
	wake_up(&tcpw.wait);
}

static int jtcp_sendmsg(struct kiocb *iocb, struct sock *sk,
			struct msghdr *msg, size_t size)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	const struct inet_sock *inet = inet_sk(sk);

	if (port == 0 || ntohs(inet->dport) == port ||
	    ntohs(inet->sport) == port) {
		printl("%d.%d.%d.%d:%u %d.%d.%d.%d:%u %d %#x %#x %u %u %u\n",
		       NIPQUAD(inet->saddr), ntohs(inet->sport),
		       NIPQUAD(inet->daddr), ntohs(inet->dport),
		       size, tp->snd_nxt, tp->snd_una,
		       tp->snd_cwnd, tcp_current_ssthresh(sk),
		       tp->snd_wnd);
	}

	jprobe_return();
	return 0;
}

static struct jprobe tcp_send_probe = {
	.kp = {
		.symbol_name	= "tcp_sendmsg",
	},
	.entry	= JPROBE_ENTRY(jtcp_sendmsg),
};


static int tcpprobe_open(struct inode * inode, struct file * file)
{
	kfifo_reset(tcpw.fifo);
	do_gettimeofday(&tcpw.tstart);
	return 0;
}

static ssize_t tcpprobe_read(struct file *file, char __user *buf,
			     size_t len, loff_t *ppos)
{
	int error = 0, cnt = 0;
	unsigned char *tbuf;

	if (!buf || len < 0)
		return -EINVAL;

	if (len == 0)
		return 0;

	tbuf = vmalloc(len);
	if (!tbuf)
		return -ENOMEM;

	error = wait_event_interruptible(tcpw.wait,
					 __kfifo_len(tcpw.fifo) != 0);
	if (error)
		goto out_free;

	cnt = kfifo_get(tcpw.fifo, tbuf, len);
	error = copy_to_user(buf, tbuf, cnt);

out_free:
	vfree(tbuf);

	return error ? error : cnt;
}

static struct file_operations tcpprobe_fops = {
	.owner	 = THIS_MODULE,
	.open	 = tcpprobe_open,
	.read    = tcpprobe_read,
};

static __init int tcpprobe_init(void)
{
	int ret = -ENOMEM;

	init_waitqueue_head(&tcpw.wait);
	spin_lock_init(&tcpw.lock);
	tcpw.fifo = kfifo_alloc(bufsize, GFP_KERNEL, &tcpw.lock);
	if (IS_ERR(tcpw.fifo))
		return PTR_ERR(tcpw.fifo);

	if (!proc_net_fops_create(procname, S_IRUSR, &tcpprobe_fops))
		goto err0;

	ret = register_jprobe(&tcp_send_probe);
	if (ret)
		goto err1;

	pr_info("TCP watch registered (port=%d)\n", port);
	return 0;
 err1:
	proc_net_remove(procname);
 err0:
	kfifo_free(tcpw.fifo);
	return ret;
}
module_init(tcpprobe_init);

static __exit void tcpprobe_exit(void)
{
	kfifo_free(tcpw.fifo);
	proc_net_remove(procname);
	unregister_jprobe(&tcp_send_probe);

}
module_exit(tcpprobe_exit);
