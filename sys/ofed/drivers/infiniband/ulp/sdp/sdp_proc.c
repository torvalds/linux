/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2008 Mellanox Technologies Ltd.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/proc_fs.h>
#include "sdp.h"

#ifdef CONFIG_PROC_FS

#define PROC_SDP_STATS "sdpstats"
#define PROC_SDP_PERF "sdpprf"

/* just like TCP fs */
struct sdp_seq_afinfo {
	struct module           *owner;
	char                    *name;
	sa_family_t             family;
	int                     (*seq_show) (struct seq_file *m, void *v);
	struct file_operations  *seq_fops;
};

struct sdp_iter_state {
	sa_family_t             family;
	int                     num;
	struct seq_operations   seq_ops;
};

static void *sdp_get_idx(struct seq_file *seq, loff_t pos)
{
	int i = 0;
	struct sdp_sock *ssk;

	if (!list_empty(&sock_list))
		list_for_each_entry(ssk, &sock_list, sock_list) {
			if (i == pos)
				return ssk;
			i++;
		}

	return NULL;
}

static void *sdp_seq_start(struct seq_file *seq, loff_t *pos)
{
	void *start = NULL;
	struct sdp_iter_state *st = seq->private;

	st->num = 0;

	if (!*pos)
		return SEQ_START_TOKEN;

	spin_lock_irq(&sock_list_lock);
	start = sdp_get_idx(seq, *pos - 1);
	if (start)
		sock_hold((struct socket *)start, SOCK_REF_SEQ);
	spin_unlock_irq(&sock_list_lock);

	return start;
}

static void *sdp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sdp_iter_state *st = seq->private;
	void *next = NULL;

	spin_lock_irq(&sock_list_lock);
	if (v == SEQ_START_TOKEN)
		next = sdp_get_idx(seq, 0);
	else
		next = sdp_get_idx(seq, *pos);
	if (next)
		sock_hold((struct socket *)next, SOCK_REF_SEQ);
	spin_unlock_irq(&sock_list_lock);

	*pos += 1;
	st->num++;

	return next;
}

static void sdp_seq_stop(struct seq_file *seq, void *v)
{
}

#define TMPSZ 150

static int sdp_seq_show(struct seq_file *seq, void *v)
{
	struct sdp_iter_state *st;
	struct socket *sk = v;
	char tmpbuf[TMPSZ + 1];
	unsigned int dest;
	unsigned int src;
	int uid;
	unsigned long inode;
	__u16 destp;
	__u16 srcp;
	__u32 rx_queue, tx_queue;

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, "%-*s\n", TMPSZ - 1,
				"  sl  local_address rem_address        "
				"uid inode   rx_queue tx_queue state");
		goto out;
	}

	st = seq->private;

	dest = inet_sk(sk)->daddr;
	src = inet_sk(sk)->rcv_saddr;
	destp = ntohs(inet_sk(sk)->dport);
	srcp = ntohs(inet_sk(sk)->sport);
	uid = sock_i_uid(sk);
	inode = sock_i_ino(sk);
	rx_queue = rcv_nxt(sdp_sk(sk)) - sdp_sk(sk)->copied_seq;
	tx_queue = sdp_sk(sk)->write_seq - sdp_sk(sk)->tx_ring.una_seq;

	sprintf(tmpbuf, "%4d: %08X:%04X %08X:%04X %5d %lu	%08X:%08X %X",
		st->num, src, srcp, dest, destp, uid, inode,
		rx_queue, tx_queue, sk->sk_state);

	seq_printf(seq, "%-*s\n", TMPSZ - 1, tmpbuf);

	sock_put(sk, SOCK_REF_SEQ);
out:
	return 0;
}

static int sdp_seq_open(struct inode *inode, struct file *file)
{
	struct sdp_seq_afinfo *afinfo = PDE(inode)->data;
	struct seq_file *seq;
	struct sdp_iter_state *s;
	int rc;

	if (unlikely(afinfo == NULL))
		return -EINVAL;

/* Workaround bogus warning by memtrack */
#define _kzalloc(size,flags) kzalloc(size,flags)
#undef kzalloc
	s = kzalloc(sizeof(*s), GFP_KERNEL);
#define kzalloc(s,f) _kzalloc(s,f)	
	if (!s)
		return -ENOMEM;
	s->family               = afinfo->family;
	s->seq_ops.start        = sdp_seq_start;
	s->seq_ops.next         = sdp_seq_next;
	s->seq_ops.show         = afinfo->seq_show;
	s->seq_ops.stop         = sdp_seq_stop;

	rc = seq_open(file, &s->seq_ops);
	if (rc)
		goto out_kfree;
	seq          = file->private_data;
	seq->private = s;
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}


static struct file_operations sdp_seq_fops;
static struct sdp_seq_afinfo sdp_seq_afinfo = {
	.owner          = THIS_MODULE,
	.name           = "sdp",
	.family         = AF_INET_SDP,
	.seq_show       = sdp_seq_show,
	.seq_fops       = &sdp_seq_fops,
};

#ifdef SDPSTATS_ON
DEFINE_PER_CPU(struct sdpstats, sdpstats);

static void sdpstats_seq_hist(struct seq_file *seq, char *str, u32 *h, int n,
		int is_log)
{
	int i;
	u32 max = 0;

	seq_printf(seq, "%s:\n", str);

	for (i = 0; i < n; i++) {
		if (h[i] > max)
			max = h[i];
	}

	if (max == 0) {
		seq_printf(seq, " - all values are 0\n");
		return;
	}

	for (i = 0; i < n; i++) {
		char s[51];
		int j = 50 * h[i] / max;
		int val = is_log ? (i == n-1 ? 0 : 1<<i) : i;
		memset(s, '*', j);
		s[j] = '\0';

		seq_printf(seq, "%10d | %-50s - %d\n", val, s, h[i]);
	}
}

#define SDPSTATS_COUNTER_GET(var) ({ \
	u32 __val = 0;						\
	unsigned int __i;                                       \
	for_each_possible_cpu(__i)                              \
		__val += per_cpu(sdpstats, __i).var;		\
	__val;							\
})	

#define SDPSTATS_HIST_GET(hist, hist_len, sum) ({ \
	unsigned int __i;                                       \
	for_each_possible_cpu(__i) {                            \
		unsigned int __j;				\
		u32 *h = per_cpu(sdpstats, __i).hist;		\
		for (__j = 0; __j < hist_len; __j++) { 		\
			sum[__j] += h[__j];			\
		} \
	} 							\
})

#define __sdpstats_seq_hist(seq, msg, hist, is_log) ({		\
	u32 tmp_hist[SDPSTATS_MAX_HIST_SIZE];			\
	int hist_len = ARRAY_SIZE(__get_cpu_var(sdpstats).hist);\
	memset(tmp_hist, 0, sizeof(tmp_hist));			\
	SDPSTATS_HIST_GET(hist, hist_len, tmp_hist);	\
	sdpstats_seq_hist(seq, msg, tmp_hist, hist_len, is_log);\
})

static int sdpstats_seq_show(struct seq_file *seq, void *v)
{
	int i;

	seq_printf(seq, "SDP statistics:\n");

	__sdpstats_seq_hist(seq, "sendmsg_seglen", sendmsg_seglen, 1);
	__sdpstats_seq_hist(seq, "send_size", send_size, 1);
	__sdpstats_seq_hist(seq, "credits_before_update",
		credits_before_update, 0);

	seq_printf(seq, "sdp_sendmsg() calls\t\t: %d\n",
		SDPSTATS_COUNTER_GET(sendmsg));
	seq_printf(seq, "bcopy segments     \t\t: %d\n",
		SDPSTATS_COUNTER_GET(sendmsg_bcopy_segment));
	seq_printf(seq, "bzcopy segments    \t\t: %d\n",
		SDPSTATS_COUNTER_GET(sendmsg_bzcopy_segment));
	seq_printf(seq, "zcopy segments    \t\t: %d\n",
		SDPSTATS_COUNTER_GET(sendmsg_zcopy_segment));
	seq_printf(seq, "post_send_credits  \t\t: %d\n",
		SDPSTATS_COUNTER_GET(post_send_credits));
	seq_printf(seq, "memcpy_count       \t\t: %u\n",
		SDPSTATS_COUNTER_GET(memcpy_count));

        for (i = 0; i < ARRAY_SIZE(__get_cpu_var(sdpstats).post_send); i++) {
                if (mid2str(i)) {
                        seq_printf(seq, "post_send %-20s\t: %d\n",
                                        mid2str(i),
					SDPSTATS_COUNTER_GET(post_send[i]));
                }
        }

	seq_printf(seq, "\n");
	seq_printf(seq, "post_recv         \t\t: %d\n",
		SDPSTATS_COUNTER_GET(post_recv));
	seq_printf(seq, "BZCopy poll miss  \t\t: %d\n",
		SDPSTATS_COUNTER_GET(bzcopy_poll_miss));
	seq_printf(seq, "send_wait_for_mem \t\t: %d\n",
		SDPSTATS_COUNTER_GET(send_wait_for_mem));
	seq_printf(seq, "send_miss_no_credits\t\t: %d\n",
		SDPSTATS_COUNTER_GET(send_miss_no_credits));

	seq_printf(seq, "rx_poll_miss      \t\t: %d\n", SDPSTATS_COUNTER_GET(rx_poll_miss));
	seq_printf(seq, "tx_poll_miss      \t\t: %d\n", SDPSTATS_COUNTER_GET(tx_poll_miss));
	seq_printf(seq, "tx_poll_busy      \t\t: %d\n", SDPSTATS_COUNTER_GET(tx_poll_busy));
	seq_printf(seq, "tx_poll_hit       \t\t: %d\n", SDPSTATS_COUNTER_GET(tx_poll_hit));

	seq_printf(seq, "CQ stats:\n");
	seq_printf(seq, "- RX interrupts\t\t: %d\n", SDPSTATS_COUNTER_GET(rx_int_count));
	seq_printf(seq, "- TX interrupts\t\t: %d\n", SDPSTATS_COUNTER_GET(tx_int_count));

	seq_printf(seq, "ZCopy stats:\n");
	seq_printf(seq, "- TX timeout\t\t: %d\n", SDPSTATS_COUNTER_GET(zcopy_tx_timeout));
	seq_printf(seq, "- TX cross send\t\t: %d\n", SDPSTATS_COUNTER_GET(zcopy_cross_send));
	seq_printf(seq, "- TX aborted by peer\t: %d\n", SDPSTATS_COUNTER_GET(zcopy_tx_aborted));
	seq_printf(seq, "- TX error\t\t: %d\n", SDPSTATS_COUNTER_GET(zcopy_tx_error));
	return 0;
}

static ssize_t sdpstats_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offs)
{
	int i;

	for_each_possible_cpu(i)
		memset(&per_cpu(sdpstats, i), 0, sizeof(struct sdpstats));
	printk(KERN_WARNING "Cleared sdp statistics\n");

	return count;
}

static int sdpstats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdpstats_seq_show, NULL);
}

static struct file_operations sdpstats_fops = {
	.owner		= THIS_MODULE,
	.open		= sdpstats_seq_open,
	.read		= seq_read,
	.write		= sdpstats_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#endif

#ifdef SDP_PROFILING
struct sdpprf_log sdpprf_log[SDPPRF_LOG_SIZE];
int sdpprf_log_count;

static unsigned long long start_t;

static int sdpprf_show(struct seq_file *m, void *v)
{
	struct sdpprf_log *l = v;
	unsigned long nsec_rem, t;

	if (!sdpprf_log_count) {
		seq_printf(m, "No performance logs\n");
		goto out;
	}

	t = l->time - start_t;
	nsec_rem = do_div(t, 1000000000);

	seq_printf(m, "%-6d: [%5lu.%06lu] %-50s - [%d{%d} %d:%d] "
			"mb: %p %s:%d\n",
			l->idx, (unsigned long)t, nsec_rem/1000,
			l->msg, l->pid, l->cpu, l->sk_num, l->sk_dport,
			l->mb, l->func, l->line);
out:
	return 0;
}

static void *sdpprf_start(struct seq_file *p, loff_t *pos)
{
	int idx = *pos;

	if (!*pos) {
		if (!sdpprf_log_count)
			return SEQ_START_TOKEN;
	}

	if (*pos >= MIN(sdpprf_log_count, SDPPRF_LOG_SIZE - 1))
		return NULL;

	if (sdpprf_log_count >= SDPPRF_LOG_SIZE - 1) {
		int off = sdpprf_log_count & (SDPPRF_LOG_SIZE - 1);
		idx = (idx + off) & (SDPPRF_LOG_SIZE - 1);

	}

	if (!start_t)
		start_t = sdpprf_log[idx].time;
	return &sdpprf_log[idx];
}

static void *sdpprf_next(struct seq_file *p, void *v, loff_t *pos)
{
	struct sdpprf_log *l = v;

	if (++*pos >= MIN(sdpprf_log_count, SDPPRF_LOG_SIZE - 1))
		return NULL;

	++l;
	if (l - &sdpprf_log[0] >= SDPPRF_LOG_SIZE - 1)
		return &sdpprf_log[0];

	return l;
}

static void sdpprf_stop(struct seq_file *p, void *v)
{
}

static struct seq_operations sdpprf_ops = {
	.start = sdpprf_start,
	.stop = sdpprf_stop,
	.next = sdpprf_next,
	.show = sdpprf_show,
};

static int sdpprf_open(struct inode *inode, struct file *file)
{
	int res;

	res = seq_open(file, &sdpprf_ops);

	return res;
}

static ssize_t sdpprf_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *offs)
{
	sdpprf_log_count = 0;
	printk(KERN_INFO "Cleared sdpprf statistics\n");

	return count;
}

static struct file_operations sdpprf_fops = {
	.open           = sdpprf_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release,
	.write		= sdpprf_write,
};
#endif /* SDP_PROFILING */

int __init sdp_proc_init(void)
{
	struct proc_dir_entry *p = NULL;
#ifdef SDPSTATS_ON
	struct proc_dir_entry *stats = NULL;
#endif
#ifdef SDP_PROFILING
	struct proc_dir_entry *prof = NULL;
#endif

	sdp_seq_afinfo.seq_fops->owner         = sdp_seq_afinfo.owner;
	sdp_seq_afinfo.seq_fops->open          = sdp_seq_open;
	sdp_seq_afinfo.seq_fops->read          = seq_read;
	sdp_seq_afinfo.seq_fops->llseek        = seq_lseek;
	sdp_seq_afinfo.seq_fops->release       = seq_release_private;

	p = proc_net_fops_create(&init_net, sdp_seq_afinfo.name, S_IRUGO,
				 sdp_seq_afinfo.seq_fops);
	if (p)
		p->data = &sdp_seq_afinfo;
	else
		goto no_mem;

#ifdef SDPSTATS_ON

	stats = proc_net_fops_create(&init_net, PROC_SDP_STATS,
			S_IRUGO | S_IWUGO, &sdpstats_fops);
	if (!stats)
		goto no_mem_stats;

#endif

#ifdef SDP_PROFILING
	prof = proc_net_fops_create(&init_net, PROC_SDP_PERF,
			S_IRUGO | S_IWUGO, &sdpprf_fops);
	if (!prof)
		goto no_mem_prof;
#endif

	return 0;

#ifdef SDP_PROFILING
no_mem_prof:
#endif

#ifdef SDPSTATS_ON
	proc_net_remove(&init_net, PROC_SDP_STATS);

no_mem_stats:
#endif
	proc_net_remove(&init_net, sdp_seq_afinfo.name);

no_mem:	
	return -ENOMEM;
}

void sdp_proc_unregister(void)
{
	proc_net_remove(&init_net, sdp_seq_afinfo.name);
	memset(sdp_seq_afinfo.seq_fops, 0, sizeof(*sdp_seq_afinfo.seq_fops));

#ifdef SDPSTATS_ON
	proc_net_remove(&init_net, PROC_SDP_STATS);
#endif
#ifdef SDP_PROFILING
	proc_net_remove(&init_net, PROC_SDP_PERF);
#endif
}

#else /* CONFIG_PROC_FS */

int __init sdp_proc_init(void)
{
	return 0;
}

void sdp_proc_unregister(void)
{

}
#endif /* CONFIG_PROC_FS */
