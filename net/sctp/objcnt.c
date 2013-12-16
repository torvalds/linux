/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 *
 * This file is part of the SCTP kernel implementation
 *
 * Support for memory object debugging.  This allows one to monitor the
 * object allocations/deallocations for types instrumented for this
 * via the proc fs.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Jon Grimm             <jgrimm@us.ibm.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <net/sctp/sctp.h>

/*
 * Global counters to count raw object allocation counts.
 * To add new counters, choose a unique suffix for the variable
 * name as the helper macros key off this suffix to make
 * life easier for the programmer.
 */

SCTP_DBG_OBJCNT(sock);
SCTP_DBG_OBJCNT(ep);
SCTP_DBG_OBJCNT(transport);
SCTP_DBG_OBJCNT(assoc);
SCTP_DBG_OBJCNT(bind_addr);
SCTP_DBG_OBJCNT(bind_bucket);
SCTP_DBG_OBJCNT(chunk);
SCTP_DBG_OBJCNT(addr);
SCTP_DBG_OBJCNT(ssnmap);
SCTP_DBG_OBJCNT(datamsg);
SCTP_DBG_OBJCNT(keys);

/* An array to make it easy to pretty print the debug information
 * to the proc fs.
 */
static sctp_dbg_objcnt_entry_t sctp_dbg_objcnt[] = {
	SCTP_DBG_OBJCNT_ENTRY(sock),
	SCTP_DBG_OBJCNT_ENTRY(ep),
	SCTP_DBG_OBJCNT_ENTRY(assoc),
	SCTP_DBG_OBJCNT_ENTRY(transport),
	SCTP_DBG_OBJCNT_ENTRY(chunk),
	SCTP_DBG_OBJCNT_ENTRY(bind_addr),
	SCTP_DBG_OBJCNT_ENTRY(bind_bucket),
	SCTP_DBG_OBJCNT_ENTRY(addr),
	SCTP_DBG_OBJCNT_ENTRY(ssnmap),
	SCTP_DBG_OBJCNT_ENTRY(datamsg),
	SCTP_DBG_OBJCNT_ENTRY(keys),
};

/* Callback from procfs to read out objcount information.
 * Walk through the entries in the sctp_dbg_objcnt array, dumping
 * the raw object counts for each monitored type.
 */
static int sctp_objcnt_seq_show(struct seq_file *seq, void *v)
{
	int i;

	i = (int)*(loff_t *)v;
	seq_setwidth(seq, 127);
	seq_printf(seq, "%s: %d", sctp_dbg_objcnt[i].label,
				atomic_read(sctp_dbg_objcnt[i].counter));
	seq_pad(seq, '\n');
	return 0;
}

static void *sctp_objcnt_seq_start(struct seq_file *seq, loff_t *pos)
{
	return (*pos >= ARRAY_SIZE(sctp_dbg_objcnt)) ? NULL : (void *)pos;
}

static void sctp_objcnt_seq_stop(struct seq_file *seq, void *v)
{
}

static void * sctp_objcnt_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return (*pos >= ARRAY_SIZE(sctp_dbg_objcnt)) ? NULL : (void *)pos;
}

static const struct seq_operations sctp_objcnt_seq_ops = {
	.start = sctp_objcnt_seq_start,
	.next  = sctp_objcnt_seq_next,
	.stop  = sctp_objcnt_seq_stop,
	.show  = sctp_objcnt_seq_show,
};

static int sctp_objcnt_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &sctp_objcnt_seq_ops);
}

static const struct file_operations sctp_objcnt_ops = {
	.open	 = sctp_objcnt_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

/* Initialize the objcount in the proc filesystem.  */
void sctp_dbg_objcnt_init(struct net *net)
{
	struct proc_dir_entry *ent;

	ent = proc_create("sctp_dbg_objcnt", 0,
			  net->sctp.proc_net_sctp, &sctp_objcnt_ops);
	if (!ent)
		pr_warn("sctp_dbg_objcnt: Unable to create /proc entry.\n");
}

/* Cleanup the objcount entry in the proc filesystem.  */
void sctp_dbg_objcnt_exit(struct net *net)
{
	remove_proc_entry("sctp_dbg_objcnt", net->sctp.proc_net_sctp);
}


