// SPDX-License-Identifier: GPL-2.0+
/*
 * This file contains the jiffies based clocksource.
 *
 * Copyright (C) 2004, 2005 IBM, John Stultz (johnstul@us.ibm.com)
 */
#include <linux/clocksource.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/init.h>

#include "timekeeping.h"
#include "tick-internal.h"


static u64 jiffies_read(struct clocksource *cs)
{
	return (u64) jiffies;
}

/*
 * The Jiffies based clocksource is the lowest common
 * denominator clock source which should function on
 * all systems. It has the same coarse resolution as
 * the timer interrupt frequency HZ and it suffers
 * inaccuracies caused by missed or lost timer
 * interrupts and the inability for the timer
 * interrupt hardware to accurately tick at the
 * requested HZ value. It is also not recommended
 * for "tick-less" systems.
 */
static struct clocksource clocksource_jiffies = {
	.name			= "jiffies",
	.rating			= 1, /* lowest valid rating*/
	.uncertainty_margin	= 32 * NSEC_PER_MSEC,
	.read			= jiffies_read,
	.mask			= CLOCKSOURCE_MASK(32),
	.mult			= TICK_NSEC << JIFFIES_SHIFT, /* details above */
	.shift			= JIFFIES_SHIFT,
	.max_cycles		= 10,
};

__cacheline_aligned_in_smp DEFINE_RAW_SPINLOCK(jiffies_lock);
__cacheline_aligned_in_smp seqcount_raw_spinlock_t jiffies_seq =
	SEQCNT_RAW_SPINLOCK_ZERO(jiffies_seq, &jiffies_lock);

#if (BITS_PER_LONG < 64)
u64 get_jiffies_64(void)
{
	unsigned int seq;
	u64 ret;

	do {
		seq = read_seqcount_begin(&jiffies_seq);
		ret = jiffies_64;
	} while (read_seqcount_retry(&jiffies_seq, seq));
	return ret;
}
EXPORT_SYMBOL(get_jiffies_64);
#endif

EXPORT_SYMBOL(jiffies);

static int __init init_jiffies_clocksource(void)
{
	return __clocksource_register(&clocksource_jiffies);
}

core_initcall(init_jiffies_clocksource);

struct clocksource * __init __weak clocksource_default_clock(void)
{
	return &clocksource_jiffies;
}

static struct clocksource refined_jiffies;

void __init register_refined_jiffies(long cycles_per_second)
{
	u64 nsec_per_tick, shift_hz;
	long cycles_per_tick;

	refined_jiffies = clocksource_jiffies;
	refined_jiffies.name = "refined-jiffies";
	refined_jiffies.rating++;

	/* Calc cycles per tick */
	cycles_per_tick = (cycles_per_second + HZ/2)/HZ;
	/* shift_hz stores hz<<8 for extra accuracy */
	shift_hz = (u64)cycles_per_second << 8;
	shift_hz += cycles_per_tick/2;
	do_div(shift_hz, cycles_per_tick);
	/* Calculate nsec_per_tick using shift_hz */
	nsec_per_tick = (u64)NSEC_PER_SEC << 8;
	nsec_per_tick += (u32)shift_hz/2;
	do_div(nsec_per_tick, (u32)shift_hz);

	refined_jiffies.mult = ((u32)nsec_per_tick) << JIFFIES_SHIFT;

	__clocksource_register(&refined_jiffies);
}

#ifdef CONFIG_PROC_SYSCTL
static ulong mult_hz(const ulong val)
{
	return val * HZ;
}

static ulong div_hz(const ulong val)
{
	return val / HZ;
}

static int sysctl_u2k_int_conv_hz(const bool *negp, const ulong *u_ptr, int *k_ptr)
{
	return proc_int_u2k_conv_uop(u_ptr, k_ptr, negp, mult_hz);
}

static int sysctl_k2u_int_conv_hz(bool *negp, ulong *u_ptr, const int *k_ptr)
{
	return proc_int_k2u_conv_kop(u_ptr, k_ptr, negp, div_hz);
}

static int sysctl_u2k_int_conv_userhz(const bool *negp, const ulong *u_ptr, int *k_ptr)
{
	return proc_int_u2k_conv_uop(u_ptr, k_ptr, negp, clock_t_to_jiffies);
}

static ulong sysctl_jiffies_to_clock_t(const ulong val)
{
	return jiffies_to_clock_t(val);
}

static int sysctl_k2u_int_conv_userhz(bool *negp, ulong *u_ptr, const int *k_ptr)
{
	return proc_int_k2u_conv_kop(u_ptr, k_ptr, negp, sysctl_jiffies_to_clock_t);
}

static ulong sysctl_msecs_to_jiffies(const ulong val)
{
	return msecs_to_jiffies(val);
}

static int sysctl_u2k_int_conv_ms(const bool *negp, const ulong *u_ptr, int *k_ptr)
{
	return proc_int_u2k_conv_uop(u_ptr, k_ptr, negp, sysctl_msecs_to_jiffies);
}

static ulong sysctl_jiffies_to_msecs(const ulong val)
{
	return jiffies_to_msecs(val);
}

static int sysctl_k2u_int_conv_ms(bool *negp, ulong *u_ptr, const int *k_ptr)
{
	return proc_int_k2u_conv_kop(u_ptr, k_ptr, negp, sysctl_jiffies_to_msecs);
}

static int do_proc_int_conv_jiffies(bool *negp, ulong *u_ptr, int *k_ptr,
				    int dir, const struct ctl_table *tbl)
{
	return proc_int_conv(negp, u_ptr, k_ptr, dir, tbl, false,
			     sysctl_u2k_int_conv_hz, sysctl_k2u_int_conv_hz);
}

static int do_proc_int_conv_userhz_jiffies(bool *negp, ulong *u_ptr,
					   int *k_ptr, int dir,
					   const struct ctl_table *tbl)
{
	return proc_int_conv(negp, u_ptr, k_ptr, dir, tbl, false,
			     sysctl_u2k_int_conv_userhz,
			     sysctl_k2u_int_conv_userhz);
}

static int do_proc_int_conv_ms_jiffies(bool *negp, ulong *u_ptr, int *k_ptr,
				       int dir, const struct ctl_table *tbl)
{
	return proc_int_conv(negp, u_ptr, k_ptr, dir, tbl, false,
			     sysctl_u2k_int_conv_ms, sysctl_k2u_int_conv_ms);
}

static int do_proc_int_conv_ms_jiffies_minmax(bool *negp, ulong *u_ptr,
					      int *k_ptr, int dir,
					      const struct ctl_table *tbl)
{
	return proc_int_conv(negp, u_ptr, k_ptr, dir, tbl, false,
			     sysctl_u2k_int_conv_ms, sysctl_k2u_int_conv_ms);
}

#else // CONFIG_PROC_SYSCTL
static int do_proc_int_conv_jiffies(bool *negp, ulong *u_ptr, int *k_ptr,
				    int dir, const struct ctl_table *tbl)
{
	return -ENOSYS;
}

static int do_proc_int_conv_userhz_jiffies(bool *negp, ulong *u_ptr,
					   int *k_ptr, int dir,
					   const struct ctl_table *tbl)
{
	return -ENOSYS;
}

static int do_proc_int_conv_ms_jiffies(bool *negp, ulong *u_ptr, int *k_ptr,
				       int dir, const struct ctl_table *tbl)
{
	return -ENOSYS;
}

static int do_proc_int_conv_ms_jiffies_minmax(bool *negp, ulong *u_ptr,
					      int *k_ptr, int dir,
					      const struct ctl_table *tbl)
{
	return -ENOSYS;
}
#endif

/**
 * proc_dointvec_jiffies - read a vector of integers as seconds
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in seconds, and are converted into
 * jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_jiffies(const struct ctl_table *table, int dir,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_dointvec_conv(table, dir, buffer, lenp, ppos,
				  do_proc_int_conv_jiffies);
}
EXPORT_SYMBOL(proc_dointvec_jiffies);

/**
 * proc_dointvec_userhz_jiffies - read a vector of integers as 1/USER_HZ seconds
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: pointer to the file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in 1/USER_HZ seconds, and
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_userhz_jiffies(const struct ctl_table *table, int dir,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	if (SYSCTL_USER_TO_KERN(dir) && USER_HZ < HZ)
		return -EINVAL;
	return proc_dointvec_conv(table, dir, buffer, lenp, ppos,
				  do_proc_int_conv_userhz_jiffies);
}
EXPORT_SYMBOL(proc_dointvec_userhz_jiffies);

/**
 * proc_dointvec_ms_jiffies - read a vector of integers as 1 milliseconds
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: the current position in the file
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 * The values read are assumed to be in 1/1000 seconds, and
 * are converted into jiffies.
 *
 * Returns 0 on success.
 */
int proc_dointvec_ms_jiffies(const struct ctl_table *table, int dir, void *buffer,
		size_t *lenp, loff_t *ppos)
{
	return proc_dointvec_conv(table, dir, buffer, lenp, ppos,
				  do_proc_int_conv_ms_jiffies);
}
EXPORT_SYMBOL(proc_dointvec_ms_jiffies);

int proc_dointvec_ms_jiffies_minmax(const struct ctl_table *table, int dir,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_dointvec_conv(table, dir, buffer, lenp, ppos,
				  do_proc_int_conv_ms_jiffies_minmax);
}

/**
 * proc_doulongvec_ms_jiffies_minmax - read a vector of millisecond values with min/max values
 * @table: the sysctl table
 * @dir: %TRUE if this is a write to the sysctl file
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 * @ppos: file position
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned long) unsigned long
 * values from/to the user buffer, treated as an ASCII string. The values
 * are treated as milliseconds, and converted to jiffies when they are stored.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_doulongvec_ms_jiffies_minmax(const struct ctl_table *table, int dir,
				      void *buffer, size_t *lenp, loff_t *ppos)
{
	return proc_doulongvec_minmax_conv(table, dir, buffer, lenp, ppos,
					   HZ, 1000l);
}
EXPORT_SYMBOL(proc_doulongvec_ms_jiffies_minmax);

