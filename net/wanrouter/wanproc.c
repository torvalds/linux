/*****************************************************************************
* wanproc.c	WAN Router Module. /proc filesystem interface.
*
*		This module is completely hardware-independent and provides
*		access to the router using Linux /proc filesystem.
*
* Author: 	Gideon Hack
*
* Copyright:	(c) 1995-1999 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 02, 1999  Gideon Hack	Updates for Linux 2.2.X kernels.
* Jun 29, 1997	Alan Cox	Merged with 1.0.3 vendor code
* Jan 29, 1997	Gene Kozin	v1.0.1. Implemented /proc read routines
* Jan 30, 1997	Alan Cox	Hacked around for 2.1
* Dec 13, 1996	Gene Kozin	Initial version (based on Sangoma's WANPIPE)
*****************************************************************************/

#include <linux/init.h>		/* __initfunc et al. */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wanrouter.h>	/* WAN router API definitions */
#include <linux/seq_file.h>
#include <linux/smp_lock.h>

#include <asm/io.h>

#define PROC_STATS_FORMAT "%30s: %12lu\n"

/****** Defines and Macros **************************************************/

#define PROT_DECODE(prot) ((prot == WANCONFIG_FR) ? " FR" :\
			      (prot == WANCONFIG_X25) ? " X25" : \
				 (prot == WANCONFIG_PPP) ? " PPP" : \
				    (prot == WANCONFIG_CHDLC) ? " CHDLC": \
				       (prot == WANCONFIG_MPPP) ? " MPPP" : \
					   " Unknown" )

/****** Function Prototypes *************************************************/

#ifdef CONFIG_PROC_FS

/* Miscellaneous */

/*
 *	Structures for interfacing with the /proc filesystem.
 *	Router creates its own directory /proc/net/router with the folowing
 *	entries:
 *	config		device configuration
 *	status		global device statistics
 *	<device>	entry for each WAN device
 */

/*
 *	Generic /proc/net/router/<file> file and inode operations
 */

/*
 *	/proc/net/router
 */

static struct proc_dir_entry *proc_router;

/* Strings */

/*
 *	Interface functions
 */

/****** Proc filesystem entry points ****************************************/

/*
 *	Iterator
 */
static void *r_start(struct seq_file *m, loff_t *pos)
{
	struct wan_device *wandev;
	loff_t l = *pos;

	lock_kernel();
	if (!l--)
		return SEQ_START_TOKEN;
	for (wandev = wanrouter_router_devlist; l-- && wandev;
	     wandev = wandev->next)
		;
	return wandev;
}

static void *r_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct wan_device *wandev = v;
	(*pos)++;
	return (v == SEQ_START_TOKEN) ? wanrouter_router_devlist : wandev->next;
}

static void r_stop(struct seq_file *m, void *v)
{
	unlock_kernel();
}

static int config_show(struct seq_file *m, void *v)
{
	struct wan_device *p = v;
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "Device name    | port |IRQ|DMA|  mem.addr  |"
			    "mem.size|option1|option2|option3|option4\n");
		return 0;
	}
	if (!p->state)
		return 0;
	seq_printf(m, "%-15s|0x%-4X|%3u|%3u| 0x%-8lX |0x%-6X|%7u|%7u|%7u|%7u\n",
			p->name, p->ioport, p->irq, p->dma, p->maddr, p->msize,
			p->hw_opt[0], p->hw_opt[1], p->hw_opt[2], p->hw_opt[3]);
	return 0;
}

static int status_show(struct seq_file *m, void *v)
{
	struct wan_device *p = v;
	if (v == SEQ_START_TOKEN) {
		seq_puts(m, "Device name    |protocol|station|interface|"
			    "clocking|baud rate| MTU |ndev|link state\n");
		return 0;
	}
	if (!p->state)
		return 0;
	seq_printf(m, "%-15s|%-8s| %-7s| %-9s|%-8s|%9u|%5u|%3u |",
		p->name,
		PROT_DECODE(p->config_id),
		p->config_id == WANCONFIG_FR ?
			(p->station ? "Node" : "CPE") :
			(p->config_id == WANCONFIG_X25 ?
			(p->station ? "DCE" : "DTE") :
			("N/A")),
		p->interface ? "V.35" : "RS-232",
		p->clocking ? "internal" : "external",
		p->bps,
		p->mtu,
		p->ndev);

	switch (p->state) {
	case WAN_UNCONFIGURED:
		seq_printf(m, "%-12s\n", "unconfigured");
		break;
	case WAN_DISCONNECTED:
		seq_printf(m, "%-12s\n", "disconnected");
		break;
	case WAN_CONNECTING:
		seq_printf(m, "%-12s\n", "connecting");
		break;
	case WAN_CONNECTED:
		seq_printf(m, "%-12s\n", "connected");
		break;
	default:
		seq_printf(m, "%-12s\n", "invalid");
		break;
	}
	return 0;
}

static const struct seq_operations config_op = {
	.start	= r_start,
	.next	= r_next,
	.stop	= r_stop,
	.show	= config_show,
};

static const struct seq_operations status_op = {
	.start	= r_start,
	.next	= r_next,
	.stop	= r_stop,
	.show	= status_show,
};

static int config_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &config_op);
}

static int status_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &status_op);
}

static const struct file_operations config_fops = {
	.owner	 = THIS_MODULE,
	.open	 = config_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

static const struct file_operations status_fops = {
	.owner	 = THIS_MODULE,
	.open	 = status_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

static int wandev_show(struct seq_file *m, void *v)
{
	struct wan_device *wandev = m->private;

	if (wandev->magic != ROUTER_MAGIC)
		return 0;

	if (!wandev->state) {
		seq_puts(m, "device is not configured!\n");
		return 0;
	}

	/* Update device statistics */
	if (wandev->update) {
		int err = wandev->update(wandev);
		if (err == -EAGAIN) {
			seq_puts(m, "Device is busy!\n");
			return 0;
		}
		if (err) {
			seq_puts(m, "Device is not configured!\n");
			return 0;
		}
	}

	seq_printf(m, PROC_STATS_FORMAT,
		"total packets received", wandev->stats.rx_packets);
	seq_printf(m, PROC_STATS_FORMAT,
		"total packets transmitted", wandev->stats.tx_packets);
	seq_printf(m, PROC_STATS_FORMAT,
		"total bytes received", wandev->stats.rx_bytes);
	seq_printf(m, PROC_STATS_FORMAT,
		"total bytes transmitted", wandev->stats.tx_bytes);
	seq_printf(m, PROC_STATS_FORMAT,
		"bad packets received", wandev->stats.rx_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"packet transmit problems", wandev->stats.tx_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"received frames dropped", wandev->stats.rx_dropped);
	seq_printf(m, PROC_STATS_FORMAT,
		"transmit frames dropped", wandev->stats.tx_dropped);
	seq_printf(m, PROC_STATS_FORMAT,
		"multicast packets received", wandev->stats.multicast);
	seq_printf(m, PROC_STATS_FORMAT,
		"transmit collisions", wandev->stats.collisions);
	seq_printf(m, PROC_STATS_FORMAT,
		"receive length errors", wandev->stats.rx_length_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"receiver overrun errors", wandev->stats.rx_over_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"CRC errors", wandev->stats.rx_crc_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"frame format errors (aborts)", wandev->stats.rx_frame_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"receiver fifo overrun", wandev->stats.rx_fifo_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"receiver missed packet", wandev->stats.rx_missed_errors);
	seq_printf(m, PROC_STATS_FORMAT,
		"aborted frames transmitted", wandev->stats.tx_aborted_errors);
	return 0;
}

static int wandev_open(struct inode *inode, struct file *file)
{
	return single_open(file, wandev_show, PDE(inode)->data);
}

static const struct file_operations wandev_fops = {
	.owner	 = THIS_MODULE,
	.open	 = wandev_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
	.ioctl	 = wanrouter_ioctl,
};

/*
 *	Initialize router proc interface.
 */

int __init wanrouter_proc_init(void)
{
	struct proc_dir_entry *p;
	proc_router = proc_mkdir(ROUTER_NAME, proc_net);
	if (!proc_router)
		goto fail;

	p = create_proc_entry("config", S_IRUGO, proc_router);
	if (!p)
		goto fail_config;
	p->proc_fops = &config_fops;
	p = create_proc_entry("status", S_IRUGO, proc_router);
	if (!p)
		goto fail_stat;
	p->proc_fops = &status_fops;
	return 0;
fail_stat:
	remove_proc_entry("config", proc_router);
fail_config:
	remove_proc_entry(ROUTER_NAME, proc_net);
fail:
	return -ENOMEM;
}

/*
 *	Clean up router proc interface.
 */

void wanrouter_proc_cleanup(void)
{
	remove_proc_entry("config", proc_router);
	remove_proc_entry("status", proc_router);
	remove_proc_entry(ROUTER_NAME, proc_net);
}

/*
 *	Add directory entry for WAN device.
 */

int wanrouter_proc_add(struct wan_device* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;

	wandev->dent = create_proc_entry(wandev->name, S_IRUGO, proc_router);
	if (!wandev->dent)
		return -ENOMEM;
	wandev->dent->proc_fops	= &wandev_fops;
	wandev->dent->data	= wandev;
	return 0;
}

/*
 *	Delete directory entry for WAN device.
 */
int wanrouter_proc_delete(struct wan_device* wandev)
{
	if (wandev->magic != ROUTER_MAGIC)
		return -EINVAL;
	remove_proc_entry(wandev->name, proc_router);
	return 0;
}

#else

/*
 *	No /proc - output stubs
 */

int __init wanrouter_proc_init(void)
{
	return 0;
}

void wanrouter_proc_cleanup(void)
{
}

int wanrouter_proc_add(struct wan_device *wandev)
{
	return 0;
}

int wanrouter_proc_delete(struct wan_device *wandev)
{
	return 0;
}

#endif

/*
 *	End
 */

