/*
 * Read-Copy Update tracing for realtime implementation
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2006
 *
 * Papers:  http://www.rdrop.com/users/paulmck/RCU
 *
 * For detailed explanation of Read-Copy Update mechanism see -
 * 		Documentation/RCU/ *.txt
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/rcupdate.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/mutex.h>
#include <linux/rcupreempt_trace.h>
#include <linux/debugfs.h>

static struct mutex rcupreempt_trace_mutex;
static char *rcupreempt_trace_buf;
#define RCUPREEMPT_TRACE_BUF_SIZE 4096

void rcupreempt_trace_move2done(struct rcupreempt_trace *trace)
{
	trace->done_length += trace->wait_length;
	trace->done_add += trace->wait_length;
	trace->wait_length = 0;
}
void rcupreempt_trace_move2wait(struct rcupreempt_trace *trace)
{
	trace->wait_length += trace->next_length;
	trace->wait_add += trace->next_length;
	trace->next_length = 0;
}
void rcupreempt_trace_try_flip_1(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->rcu_try_flip_1);
}
void rcupreempt_trace_try_flip_e1(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->rcu_try_flip_e1);
}
void rcupreempt_trace_try_flip_i1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_i1++;
}
void rcupreempt_trace_try_flip_ie1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ie1++;
}
void rcupreempt_trace_try_flip_g1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_g1++;
}
void rcupreempt_trace_try_flip_a1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_a1++;
}
void rcupreempt_trace_try_flip_ae1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ae1++;
}
void rcupreempt_trace_try_flip_a2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_a2++;
}
void rcupreempt_trace_try_flip_z1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_z1++;
}
void rcupreempt_trace_try_flip_ze1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_ze1++;
}
void rcupreempt_trace_try_flip_z2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_z2++;
}
void rcupreempt_trace_try_flip_m1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_m1++;
}
void rcupreempt_trace_try_flip_me1(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_me1++;
}
void rcupreempt_trace_try_flip_m2(struct rcupreempt_trace *trace)
{
	trace->rcu_try_flip_m2++;
}
void rcupreempt_trace_check_callbacks(struct rcupreempt_trace *trace)
{
	trace->rcu_check_callbacks++;
}
void rcupreempt_trace_done_remove(struct rcupreempt_trace *trace)
{
	trace->done_remove += trace->done_length;
	trace->done_length = 0;
}
void rcupreempt_trace_invoke(struct rcupreempt_trace *trace)
{
	atomic_inc(&trace->done_invoked);
}
void rcupreempt_trace_next_add(struct rcupreempt_trace *trace)
{
	trace->next_add++;
	trace->next_length++;
}

static void rcupreempt_trace_sum(struct rcupreempt_trace *sp)
{
	struct rcupreempt_trace *cp;
	int cpu;

	memset(sp, 0, sizeof(*sp));
	for_each_possible_cpu(cpu) {
		cp = rcupreempt_trace_cpu(cpu);
		sp->next_length += cp->next_length;
		sp->next_add += cp->next_add;
		sp->wait_length += cp->wait_length;
		sp->wait_add += cp->wait_add;
		sp->done_length += cp->done_length;
		sp->done_add += cp->done_add;
		sp->done_remove += cp->done_remove;
		atomic_set(&sp->done_invoked, atomic_read(&cp->done_invoked));
		sp->rcu_check_callbacks += cp->rcu_check_callbacks;
		atomic_set(&sp->rcu_try_flip_1,
			   atomic_read(&cp->rcu_try_flip_1));
		atomic_set(&sp->rcu_try_flip_e1,
			   atomic_read(&cp->rcu_try_flip_e1));
		sp->rcu_try_flip_i1 += cp->rcu_try_flip_i1;
		sp->rcu_try_flip_ie1 += cp->rcu_try_flip_ie1;
		sp->rcu_try_flip_g1 += cp->rcu_try_flip_g1;
		sp->rcu_try_flip_a1 += cp->rcu_try_flip_a1;
		sp->rcu_try_flip_ae1 += cp->rcu_try_flip_ae1;
		sp->rcu_try_flip_a2 += cp->rcu_try_flip_a2;
		sp->rcu_try_flip_z1 += cp->rcu_try_flip_z1;
		sp->rcu_try_flip_ze1 += cp->rcu_try_flip_ze1;
		sp->rcu_try_flip_z2 += cp->rcu_try_flip_z2;
		sp->rcu_try_flip_m1 += cp->rcu_try_flip_m1;
		sp->rcu_try_flip_me1 += cp->rcu_try_flip_me1;
		sp->rcu_try_flip_m2 += cp->rcu_try_flip_m2;
	}
}

static ssize_t rcustats_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	struct rcupreempt_trace trace;
	ssize_t bcount;
	int cnt = 0;

	rcupreempt_trace_sum(&trace);
	mutex_lock(&rcupreempt_trace_mutex);
	snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE - cnt,
		 "ggp=%ld rcc=%ld\n",
		 rcu_batches_completed(),
		 trace.rcu_check_callbacks);
	snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE - cnt,
		 "na=%ld nl=%ld wa=%ld wl=%ld da=%ld dl=%ld dr=%ld di=%d\n"
		 "1=%d e1=%d i1=%ld ie1=%ld g1=%ld a1=%ld ae1=%ld a2=%ld\n"
		 "z1=%ld ze1=%ld z2=%ld m1=%ld me1=%ld m2=%ld\n",

		 trace.next_add, trace.next_length,
		 trace.wait_add, trace.wait_length,
		 trace.done_add, trace.done_length,
		 trace.done_remove, atomic_read(&trace.done_invoked),
		 atomic_read(&trace.rcu_try_flip_1),
		 atomic_read(&trace.rcu_try_flip_e1),
		 trace.rcu_try_flip_i1, trace.rcu_try_flip_ie1,
		 trace.rcu_try_flip_g1,
		 trace.rcu_try_flip_a1, trace.rcu_try_flip_ae1,
			 trace.rcu_try_flip_a2,
		 trace.rcu_try_flip_z1, trace.rcu_try_flip_ze1,
			 trace.rcu_try_flip_z2,
		 trace.rcu_try_flip_m1, trace.rcu_try_flip_me1,
			trace.rcu_try_flip_m2);
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static ssize_t rcugp_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	long oldgp = rcu_batches_completed();
	ssize_t bcount;

	mutex_lock(&rcupreempt_trace_mutex);
	synchronize_rcu();
	snprintf(rcupreempt_trace_buf, RCUPREEMPT_TRACE_BUF_SIZE,
		"oldggp=%ld  newggp=%ld\n", oldgp, rcu_batches_completed());
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static ssize_t rcuctrs_read(struct file *filp, char __user *buffer,
				size_t count, loff_t *ppos)
{
	int cnt = 0;
	int cpu;
	int f = rcu_batches_completed() & 0x1;
	ssize_t bcount;

	mutex_lock(&rcupreempt_trace_mutex);

	cnt += snprintf(&rcupreempt_trace_buf[cnt], RCUPREEMPT_TRACE_BUF_SIZE,
				"CPU last cur F M\n");
	for_each_online_cpu(cpu) {
		long *flipctr = rcupreempt_flipctr(cpu);
		cnt += snprintf(&rcupreempt_trace_buf[cnt],
				RCUPREEMPT_TRACE_BUF_SIZE - cnt,
					"%3d %4ld %3ld %d %d\n",
			       cpu,
			       flipctr[!f],
			       flipctr[f],
			       rcupreempt_flip_flag(cpu),
			       rcupreempt_mb_flag(cpu));
	}
	cnt += snprintf(&rcupreempt_trace_buf[cnt],
			RCUPREEMPT_TRACE_BUF_SIZE - cnt,
			"ggp = %ld, state = %s\n",
			rcu_batches_completed(),
			rcupreempt_try_flip_state_name());
	cnt += snprintf(&rcupreempt_trace_buf[cnt],
			RCUPREEMPT_TRACE_BUF_SIZE - cnt,
			"\n");
	bcount = simple_read_from_buffer(buffer, count, ppos,
			rcupreempt_trace_buf, strlen(rcupreempt_trace_buf));
	mutex_unlock(&rcupreempt_trace_mutex);
	return bcount;
}

static struct file_operations rcustats_fops = {
	.owner = THIS_MODULE,
	.read = rcustats_read,
};

static struct file_operations rcugp_fops = {
	.owner = THIS_MODULE,
	.read = rcugp_read,
};

static struct file_operations rcuctrs_fops = {
	.owner = THIS_MODULE,
	.read = rcuctrs_read,
};

static struct dentry *rcudir, *statdir, *ctrsdir, *gpdir;
static int rcupreempt_debugfs_init(void)
{
	rcudir = debugfs_create_dir("rcu", NULL);
	if (!rcudir)
		goto out;
	statdir = debugfs_create_file("rcustats", 0444, rcudir,
						NULL, &rcustats_fops);
	if (!statdir)
		goto free_out;

	gpdir = debugfs_create_file("rcugp", 0444, rcudir, NULL, &rcugp_fops);
	if (!gpdir)
		goto free_out;

	ctrsdir = debugfs_create_file("rcuctrs", 0444, rcudir,
						NULL, &rcuctrs_fops);
	if (!ctrsdir)
		goto free_out;
	return 0;
free_out:
	if (statdir)
		debugfs_remove(statdir);
	if (gpdir)
		debugfs_remove(gpdir);
	debugfs_remove(rcudir);
out:
	return 1;
}

static int __init rcupreempt_trace_init(void)
{
	int ret;

	mutex_init(&rcupreempt_trace_mutex);
	rcupreempt_trace_buf = kmalloc(RCUPREEMPT_TRACE_BUF_SIZE, GFP_KERNEL);
	if (!rcupreempt_trace_buf)
		return 1;
	ret = rcupreempt_debugfs_init();
	if (ret)
		kfree(rcupreempt_trace_buf);
	return ret;
}

static void __exit rcupreempt_trace_cleanup(void)
{
	debugfs_remove(statdir);
	debugfs_remove(gpdir);
	debugfs_remove(ctrsdir);
	debugfs_remove(rcudir);
	kfree(rcupreempt_trace_buf);
}


module_init(rcupreempt_trace_init);
module_exit(rcupreempt_trace_cleanup);
