/*
 * proc.c - procfs support for Protocol family CAN core module
 *
 * Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/if_arp.h>
#include <linux/can/core.h>

#include "af_can.h"

/*
 * proc filenames for the PF_CAN core
 */

#define CAN_PROC_VERSION     "version"
#define CAN_PROC_STATS       "stats"
#define CAN_PROC_RESET_STATS "reset_stats"
#define CAN_PROC_RCVLIST_ALL "rcvlist_all"
#define CAN_PROC_RCVLIST_FIL "rcvlist_fil"
#define CAN_PROC_RCVLIST_INV "rcvlist_inv"
#define CAN_PROC_RCVLIST_SFF "rcvlist_sff"
#define CAN_PROC_RCVLIST_EFF "rcvlist_eff"
#define CAN_PROC_RCVLIST_ERR "rcvlist_err"

static int user_reset;

static const char rx_list_name[][8] = {
	[RX_ERR] = "rx_err",
	[RX_ALL] = "rx_all",
	[RX_FIL] = "rx_fil",
	[RX_INV] = "rx_inv",
};

/*
 * af_can statistics stuff
 */

static void can_init_stats(struct net *net)
{
	struct s_stats *can_stats = net->can.can_stats;
	struct s_pstats *can_pstats = net->can.can_pstats;
	/*
	 * This memset function is called from a timer context (when
	 * can_stattimer is active which is the default) OR in a process
	 * context (reading the proc_fs when can_stattimer is disabled).
	 */
	memset(can_stats, 0, sizeof(struct s_stats));
	can_stats->jiffies_init = jiffies;

	can_pstats->stats_reset++;

	if (user_reset) {
		user_reset = 0;
		can_pstats->user_reset++;
	}
}

static unsigned long calc_rate(unsigned long oldjif, unsigned long newjif,
			       unsigned long count)
{
	unsigned long rate;

	if (oldjif == newjif)
		return 0;

	/* see can_stat_update() - this should NEVER happen! */
	if (count > (ULONG_MAX / HZ)) {
		printk(KERN_ERR "can: calc_rate: count exceeded! %ld\n",
		       count);
		return 99999999;
	}

	rate = (count * HZ) / (newjif - oldjif);

	return rate;
}

void can_stat_update(struct timer_list *t)
{
	struct net *net = from_timer(net, t, can.can_stattimer);
	struct s_stats *can_stats = net->can.can_stats;
	unsigned long j = jiffies; /* snapshot */

	/* restart counting in timer context on user request */
	if (user_reset)
		can_init_stats(net);

	/* restart counting on jiffies overflow */
	if (j < can_stats->jiffies_init)
		can_init_stats(net);

	/* prevent overflow in calc_rate() */
	if (can_stats->rx_frames > (ULONG_MAX / HZ))
		can_init_stats(net);

	/* prevent overflow in calc_rate() */
	if (can_stats->tx_frames > (ULONG_MAX / HZ))
		can_init_stats(net);

	/* matches overflow - very improbable */
	if (can_stats->matches > (ULONG_MAX / 100))
		can_init_stats(net);

	/* calc total values */
	if (can_stats->rx_frames)
		can_stats->total_rx_match_ratio = (can_stats->matches * 100) /
			can_stats->rx_frames;

	can_stats->total_tx_rate = calc_rate(can_stats->jiffies_init, j,
					    can_stats->tx_frames);
	can_stats->total_rx_rate = calc_rate(can_stats->jiffies_init, j,
					    can_stats->rx_frames);

	/* calc current values */
	if (can_stats->rx_frames_delta)
		can_stats->current_rx_match_ratio =
			(can_stats->matches_delta * 100) /
			can_stats->rx_frames_delta;

	can_stats->current_tx_rate = calc_rate(0, HZ, can_stats->tx_frames_delta);
	can_stats->current_rx_rate = calc_rate(0, HZ, can_stats->rx_frames_delta);

	/* check / update maximum values */
	if (can_stats->max_tx_rate < can_stats->current_tx_rate)
		can_stats->max_tx_rate = can_stats->current_tx_rate;

	if (can_stats->max_rx_rate < can_stats->current_rx_rate)
		can_stats->max_rx_rate = can_stats->current_rx_rate;

	if (can_stats->max_rx_match_ratio < can_stats->current_rx_match_ratio)
		can_stats->max_rx_match_ratio = can_stats->current_rx_match_ratio;

	/* clear values for 'current rate' calculation */
	can_stats->tx_frames_delta = 0;
	can_stats->rx_frames_delta = 0;
	can_stats->matches_delta   = 0;

	/* restart timer (one second) */
	mod_timer(&net->can.can_stattimer, round_jiffies(jiffies + HZ));
}

/*
 * proc read functions
 */

static void can_print_rcvlist(struct seq_file *m, struct hlist_head *rx_list,
			      struct net_device *dev)
{
	struct receiver *r;

	hlist_for_each_entry_rcu(r, rx_list, list) {
		char *fmt = (r->can_id & CAN_EFF_FLAG)?
			"   %-5s  %08x  %08x  %pK  %pK  %8ld  %s\n" :
			"   %-5s     %03x    %08x  %pK  %pK  %8ld  %s\n";

		seq_printf(m, fmt, DNAME(dev), r->can_id, r->mask,
				r->func, r->data, r->matches, r->ident);
	}
}

static void can_print_recv_banner(struct seq_file *m)
{
	/*
	 *                  can1.  00000000  00000000  00000000
	 *                 .......          0  tp20
	 */
	seq_puts(m, "  device   can_id   can_mask  function"
			"  userdata   matches  ident\n");
}

static int can_stats_proc_show(struct seq_file *m, void *v)
{
	struct net *net = m->private;
	struct s_stats *can_stats = net->can.can_stats;
	struct s_pstats *can_pstats = net->can.can_pstats;

	seq_putc(m, '\n');
	seq_printf(m, " %8ld transmitted frames (TXF)\n", can_stats->tx_frames);
	seq_printf(m, " %8ld received frames (RXF)\n", can_stats->rx_frames);
	seq_printf(m, " %8ld matched frames (RXMF)\n", can_stats->matches);

	seq_putc(m, '\n');

	if (net->can.can_stattimer.function == can_stat_update) {
		seq_printf(m, " %8ld %% total match ratio (RXMR)\n",
				can_stats->total_rx_match_ratio);

		seq_printf(m, " %8ld frames/s total tx rate (TXR)\n",
				can_stats->total_tx_rate);
		seq_printf(m, " %8ld frames/s total rx rate (RXR)\n",
				can_stats->total_rx_rate);

		seq_putc(m, '\n');

		seq_printf(m, " %8ld %% current match ratio (CRXMR)\n",
				can_stats->current_rx_match_ratio);

		seq_printf(m, " %8ld frames/s current tx rate (CTXR)\n",
				can_stats->current_tx_rate);
		seq_printf(m, " %8ld frames/s current rx rate (CRXR)\n",
				can_stats->current_rx_rate);

		seq_putc(m, '\n');

		seq_printf(m, " %8ld %% max match ratio (MRXMR)\n",
				can_stats->max_rx_match_ratio);

		seq_printf(m, " %8ld frames/s max tx rate (MTXR)\n",
				can_stats->max_tx_rate);
		seq_printf(m, " %8ld frames/s max rx rate (MRXR)\n",
				can_stats->max_rx_rate);

		seq_putc(m, '\n');
	}

	seq_printf(m, " %8ld current receive list entries (CRCV)\n",
			can_pstats->rcv_entries);
	seq_printf(m, " %8ld maximum receive list entries (MRCV)\n",
			can_pstats->rcv_entries_max);

	if (can_pstats->stats_reset)
		seq_printf(m, "\n %8ld statistic resets (STR)\n",
				can_pstats->stats_reset);

	if (can_pstats->user_reset)
		seq_printf(m, " %8ld user statistic resets (USTR)\n",
				can_pstats->user_reset);

	seq_putc(m, '\n');
	return 0;
}

static int can_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_stats_proc_show);
}

static const struct file_operations can_stats_proc_fops = {
	.open		= can_stats_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int can_reset_stats_proc_show(struct seq_file *m, void *v)
{
	struct net *net = m->private;
	struct s_pstats *can_pstats = net->can.can_pstats;
	struct s_stats *can_stats = net->can.can_stats;

	user_reset = 1;

	if (net->can.can_stattimer.function == can_stat_update) {
		seq_printf(m, "Scheduled statistic reset #%ld.\n",
				can_pstats->stats_reset + 1);
	} else {
		if (can_stats->jiffies_init != jiffies)
			can_init_stats(net);

		seq_printf(m, "Performed statistic reset #%ld.\n",
				can_pstats->stats_reset);
	}
	return 0;
}

static int can_reset_stats_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_reset_stats_proc_show);
}

static const struct file_operations can_reset_stats_proc_fops = {
	.open		= can_reset_stats_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int can_version_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", CAN_VERSION_STRING);
	return 0;
}

static int can_version_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_version_proc_show);
}

static const struct file_operations can_version_proc_fops = {
	.open		= can_version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static inline void can_rcvlist_proc_show_one(struct seq_file *m, int idx,
					     struct net_device *dev,
					     struct can_dev_rcv_lists *d)
{
	if (!hlist_empty(&d->rx[idx])) {
		can_print_recv_banner(m);
		can_print_rcvlist(m, &d->rx[idx], dev);
	} else
		seq_printf(m, "  (%s: no entry)\n", DNAME(dev));

}

static int can_rcvlist_proc_show(struct seq_file *m, void *v)
{
	/* double cast to prevent GCC warning */
	int idx = (int)(long)PDE_DATA(m->file->f_inode);
	struct net_device *dev;
	struct can_dev_rcv_lists *d;
	struct net *net = m->private;

	seq_printf(m, "\nreceive list '%s':\n", rx_list_name[idx]);

	rcu_read_lock();

	/* receive list for 'all' CAN devices (dev == NULL) */
	d = net->can.can_rx_alldev_list;
	can_rcvlist_proc_show_one(m, idx, NULL, d);

	/* receive list for registered CAN devices */
	for_each_netdev_rcu(net, dev) {
		if (dev->type == ARPHRD_CAN && dev->ml_priv)
			can_rcvlist_proc_show_one(m, idx, dev, dev->ml_priv);
	}

	rcu_read_unlock();

	seq_putc(m, '\n');
	return 0;
}

static int can_rcvlist_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_rcvlist_proc_show);
}

static const struct file_operations can_rcvlist_proc_fops = {
	.open		= can_rcvlist_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static inline void can_rcvlist_proc_show_array(struct seq_file *m,
					       struct net_device *dev,
					       struct hlist_head *rcv_array,
					       unsigned int rcv_array_sz)
{
	unsigned int i;
	int all_empty = 1;

	/* check whether at least one list is non-empty */
	for (i = 0; i < rcv_array_sz; i++)
		if (!hlist_empty(&rcv_array[i])) {
			all_empty = 0;
			break;
		}

	if (!all_empty) {
		can_print_recv_banner(m);
		for (i = 0; i < rcv_array_sz; i++) {
			if (!hlist_empty(&rcv_array[i]))
				can_print_rcvlist(m, &rcv_array[i], dev);
		}
	} else
		seq_printf(m, "  (%s: no entry)\n", DNAME(dev));
}

static int can_rcvlist_sff_proc_show(struct seq_file *m, void *v)
{
	struct net_device *dev;
	struct can_dev_rcv_lists *d;
	struct net *net = m->private;

	/* RX_SFF */
	seq_puts(m, "\nreceive list 'rx_sff':\n");

	rcu_read_lock();

	/* sff receive list for 'all' CAN devices (dev == NULL) */
	d = net->can.can_rx_alldev_list;
	can_rcvlist_proc_show_array(m, NULL, d->rx_sff, ARRAY_SIZE(d->rx_sff));

	/* sff receive list for registered CAN devices */
	for_each_netdev_rcu(net, dev) {
		if (dev->type == ARPHRD_CAN && dev->ml_priv) {
			d = dev->ml_priv;
			can_rcvlist_proc_show_array(m, dev, d->rx_sff,
						    ARRAY_SIZE(d->rx_sff));
		}
	}

	rcu_read_unlock();

	seq_putc(m, '\n');
	return 0;
}

static int can_rcvlist_sff_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_rcvlist_sff_proc_show);
}

static const struct file_operations can_rcvlist_sff_proc_fops = {
	.open		= can_rcvlist_sff_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static int can_rcvlist_eff_proc_show(struct seq_file *m, void *v)
{
	struct net_device *dev;
	struct can_dev_rcv_lists *d;
	struct net *net = m->private;

	/* RX_EFF */
	seq_puts(m, "\nreceive list 'rx_eff':\n");

	rcu_read_lock();

	/* eff receive list for 'all' CAN devices (dev == NULL) */
	d = net->can.can_rx_alldev_list;
	can_rcvlist_proc_show_array(m, NULL, d->rx_eff, ARRAY_SIZE(d->rx_eff));

	/* eff receive list for registered CAN devices */
	for_each_netdev_rcu(net, dev) {
		if (dev->type == ARPHRD_CAN && dev->ml_priv) {
			d = dev->ml_priv;
			can_rcvlist_proc_show_array(m, dev, d->rx_eff,
						    ARRAY_SIZE(d->rx_eff));
		}
	}

	rcu_read_unlock();

	seq_putc(m, '\n');
	return 0;
}

static int can_rcvlist_eff_proc_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, can_rcvlist_eff_proc_show);
}

static const struct file_operations can_rcvlist_eff_proc_fops = {
	.open		= can_rcvlist_eff_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * can_init_proc - create main CAN proc directory and procfs entries
 */
void can_init_proc(struct net *net)
{
	/* create /proc/net/can directory */
	net->can.proc_dir = proc_net_mkdir(net, "can", net->proc_net);

	if (!net->can.proc_dir) {
		printk(KERN_INFO "can: failed to create /proc/net/can . "
			   "CONFIG_PROC_FS missing?\n");
		return;
	}

	/* own procfs entries from the AF_CAN core */
	net->can.pde_version     = proc_create(CAN_PROC_VERSION, 0644,
					       net->can.proc_dir,
					       &can_version_proc_fops);
	net->can.pde_stats       = proc_create(CAN_PROC_STATS, 0644,
					       net->can.proc_dir,
					       &can_stats_proc_fops);
	net->can.pde_reset_stats = proc_create(CAN_PROC_RESET_STATS, 0644,
					       net->can.proc_dir,
					       &can_reset_stats_proc_fops);
	net->can.pde_rcvlist_err = proc_create_data(CAN_PROC_RCVLIST_ERR, 0644,
						    net->can.proc_dir,
						    &can_rcvlist_proc_fops,
						    (void *)RX_ERR);
	net->can.pde_rcvlist_all = proc_create_data(CAN_PROC_RCVLIST_ALL, 0644,
						    net->can.proc_dir,
						    &can_rcvlist_proc_fops,
						    (void *)RX_ALL);
	net->can.pde_rcvlist_fil = proc_create_data(CAN_PROC_RCVLIST_FIL, 0644,
						    net->can.proc_dir,
						    &can_rcvlist_proc_fops,
						    (void *)RX_FIL);
	net->can.pde_rcvlist_inv = proc_create_data(CAN_PROC_RCVLIST_INV, 0644,
						    net->can.proc_dir,
						    &can_rcvlist_proc_fops,
						    (void *)RX_INV);
	net->can.pde_rcvlist_eff = proc_create(CAN_PROC_RCVLIST_EFF, 0644,
					       net->can.proc_dir,
					       &can_rcvlist_eff_proc_fops);
	net->can.pde_rcvlist_sff = proc_create(CAN_PROC_RCVLIST_SFF, 0644,
					       net->can.proc_dir,
					       &can_rcvlist_sff_proc_fops);
}

/*
 * can_remove_proc - remove procfs entries and main CAN proc directory
 */
void can_remove_proc(struct net *net)
{
	if (net->can.pde_version)
		remove_proc_entry(CAN_PROC_VERSION, net->can.proc_dir);

	if (net->can.pde_stats)
		remove_proc_entry(CAN_PROC_STATS, net->can.proc_dir);

	if (net->can.pde_reset_stats)
		remove_proc_entry(CAN_PROC_RESET_STATS, net->can.proc_dir);

	if (net->can.pde_rcvlist_err)
		remove_proc_entry(CAN_PROC_RCVLIST_ERR, net->can.proc_dir);

	if (net->can.pde_rcvlist_all)
		remove_proc_entry(CAN_PROC_RCVLIST_ALL, net->can.proc_dir);

	if (net->can.pde_rcvlist_fil)
		remove_proc_entry(CAN_PROC_RCVLIST_FIL, net->can.proc_dir);

	if (net->can.pde_rcvlist_inv)
		remove_proc_entry(CAN_PROC_RCVLIST_INV, net->can.proc_dir);

	if (net->can.pde_rcvlist_eff)
		remove_proc_entry(CAN_PROC_RCVLIST_EFF, net->can.proc_dir);

	if (net->can.pde_rcvlist_sff)
		remove_proc_entry(CAN_PROC_RCVLIST_SFF, net->can.proc_dir);

	if (net->can.proc_dir)
		remove_proc_entry("can", net->proc_net);
}
