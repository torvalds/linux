/*********************************************************************
 *
 * Filename:      irsysctl.c
 * Version:       1.0
 * Description:   Sysctl interface for IrDA
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 24 22:12:06 1998
 * Modified at:   Fri Jun  4 02:50:15 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1997, 1999 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/mm.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/init.h>

#include <net/irda/irda.h>		/* irda_debug */
#include <net/irda/irlmp.h>
#include <net/irda/timer.h>
#include <net/irda/irias_object.h>

extern int  sysctl_discovery;
extern int  sysctl_discovery_slots;
extern int  sysctl_discovery_timeout;
extern int  sysctl_slot_timeout;
extern int  sysctl_fast_poll_increase;
extern char sysctl_devname[];
extern int  sysctl_max_baud_rate;
extern int  sysctl_min_tx_turn_time;
extern int  sysctl_max_tx_data_size;
extern int  sysctl_max_tx_window;
extern int  sysctl_max_noreply_time;
extern int  sysctl_warn_noreply_time;
extern int  sysctl_lap_keepalive_time;

extern struct irlmp_cb *irlmp;

/* this is needed for the proc_dointvec_minmax - Jean II */
static int max_discovery_slots = 16;		/* ??? */
static int min_discovery_slots = 1;
/* IrLAP 6.13.2 says 25ms to 10+70ms - allow higher since some devices
 * seems to require it. (from Dag's comment) */
static int max_slot_timeout = 160;
static int min_slot_timeout = 20;
static int max_max_baud_rate = 16000000;	/* See qos.c - IrLAP spec */
static int min_max_baud_rate = 2400;
static int max_min_tx_turn_time = 10000;	/* See qos.c - IrLAP spec */
static int min_min_tx_turn_time;
static int max_max_tx_data_size = 2048;		/* See qos.c - IrLAP spec */
static int min_max_tx_data_size = 64;
static int max_max_tx_window = 7;		/* See qos.c - IrLAP spec */
static int min_max_tx_window = 1;
static int max_max_noreply_time = 40;		/* See qos.c - IrLAP spec */
static int min_max_noreply_time = 3;
static int max_warn_noreply_time = 3;		/* 3s == standard */
static int min_warn_noreply_time = 1;		/* 1s == min WD_TIMER */
static int max_lap_keepalive_time = 10000;	/* 10s */
static int min_lap_keepalive_time = 100;	/* 100us */
/* For other sysctl, I've no idea of the range. Maybe Dag could help
 * us on that - Jean II */

static int do_devname(ctl_table *table, int write, struct file *filp,
		      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dostring(table, write, filp, buffer, lenp, ppos);
	if (ret == 0 && write) {
		struct ias_value *val;

		val = irias_new_string_value(sysctl_devname);
		if (val)
			irias_object_change_attribute("Device", "DeviceName", val);
	}
	return ret;
}


static int do_discovery(ctl_table *table, int write, struct file *filp,
                    void __user *buffer, size_t *lenp, loff_t *ppos)
{
       int ret;

       ret = proc_dointvec(table, write, filp, buffer, lenp, ppos);
       if (ret)
	       return ret;

       if (irlmp == NULL)
	       return -ENODEV;

       if (sysctl_discovery)
	       irlmp_start_discovery_timer(irlmp, sysctl_discovery_timeout*HZ);
       else
	       del_timer_sync(&irlmp->discovery_timer);

       return ret;
}

/* One file */
static ctl_table irda_table[] = {
	{
		.ctl_name	= NET_IRDA_DISCOVERY,
		.procname	= "discovery",
		.data		= &sysctl_discovery,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &do_discovery,
		.strategy       = &sysctl_intvec
	},
	{
		.ctl_name	= NET_IRDA_DEVNAME,
		.procname	= "devname",
		.data		= sysctl_devname,
		.maxlen		= 65,
		.mode		= 0644,
		.proc_handler	= &do_devname,
		.strategy	= &sysctl_string
	},
#ifdef CONFIG_IRDA_DEBUG
	{
		.ctl_name	= NET_IRDA_DEBUG,
		.procname	= "debug",
		.data		= &irda_debug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif
#ifdef CONFIG_IRDA_FAST_RR
	{
		.ctl_name	= NET_IRDA_FAST_POLL,
		.procname	= "fast_poll_increase",
		.data		= &sysctl_fast_poll_increase,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif
	{
		.ctl_name	= NET_IRDA_DISCOVERY_SLOTS,
		.procname	= "discovery_slots",
		.data		= &sysctl_discovery_slots,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_discovery_slots,
		.extra2		= &max_discovery_slots
	},
	{
		.ctl_name	= NET_IRDA_DISCOVERY_TIMEOUT,
		.procname	= "discovery_timeout",
		.data		= &sysctl_discovery_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_IRDA_SLOT_TIMEOUT,
		.procname	= "slot_timeout",
		.data		= &sysctl_slot_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_slot_timeout,
		.extra2		= &max_slot_timeout
	},
	{
		.ctl_name	= NET_IRDA_MAX_BAUD_RATE,
		.procname	= "max_baud_rate",
		.data		= &sysctl_max_baud_rate,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_max_baud_rate,
		.extra2		= &max_max_baud_rate
	},
	{
		.ctl_name	= NET_IRDA_MIN_TX_TURN_TIME,
		.procname	= "min_tx_turn_time",
		.data		= &sysctl_min_tx_turn_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_min_tx_turn_time,
		.extra2		= &max_min_tx_turn_time
	},
	{
		.ctl_name	= NET_IRDA_MAX_TX_DATA_SIZE,
		.procname	= "max_tx_data_size",
		.data		= &sysctl_max_tx_data_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_max_tx_data_size,
		.extra2		= &max_max_tx_data_size
	},
	{
		.ctl_name	= NET_IRDA_MAX_TX_WINDOW,
		.procname	= "max_tx_window",
		.data		= &sysctl_max_tx_window,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_max_tx_window,
		.extra2		= &max_max_tx_window
	},
	{
		.ctl_name	= NET_IRDA_MAX_NOREPLY_TIME,
		.procname	= "max_noreply_time",
		.data		= &sysctl_max_noreply_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_max_noreply_time,
		.extra2		= &max_max_noreply_time
	},
	{
		.ctl_name	= NET_IRDA_WARN_NOREPLY_TIME,
		.procname	= "warn_noreply_time",
		.data		= &sysctl_warn_noreply_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_warn_noreply_time,
		.extra2		= &max_warn_noreply_time
	},
	{
		.ctl_name	= NET_IRDA_LAP_KEEPALIVE_TIME,
		.procname	= "lap_keepalive_time",
		.data		= &sysctl_lap_keepalive_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_lap_keepalive_time,
		.extra2		= &max_lap_keepalive_time
	},
	{ .ctl_name = 0 }
};

static struct ctl_path irda_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "irda", .ctl_name = NET_IRDA, },
	{ }
};

static struct ctl_table_header *irda_table_header;

/*
 * Function irda_sysctl_register (void)
 *
 *    Register our sysctl interface
 *
 */
int __init irda_sysctl_register(void)
{
	irda_table_header = register_sysctl_paths(irda_path, irda_table);
	if (!irda_table_header)
		return -ENOMEM;

	return 0;
}

/*
 * Function irda_sysctl_unregister (void)
 *
 *    Unregister our sysctl interface
 *
 */
void irda_sysctl_unregister(void)
{
	unregister_sysctl_table(irda_table_header);
}



