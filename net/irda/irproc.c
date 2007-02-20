/*********************************************************************
 *
 * Filename:      irproc.c
 * Version:       1.0
 * Description:   Various entries in the /proc file system
 * Status:        Experimental.
 * Author:        Thomas Davis, <ratbert@radiks.net>
 * Created at:    Sat Feb 21 21:33:24 1998
 * Modified at:   Sun Nov 14 08:54:54 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999, Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1998, Thomas Davis, <ratbert@radiks.net>,
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     I, Thomas Davis, provide no warranty for any of this software.
 *     This material is provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>

extern struct file_operations discovery_seq_fops;
extern struct file_operations irlap_seq_fops;
extern struct file_operations irlmp_seq_fops;
extern struct file_operations irttp_seq_fops;
extern struct file_operations irias_seq_fops;

struct irda_entry {
	const char *name;
	struct file_operations *fops;
};

struct proc_dir_entry *proc_irda;
EXPORT_SYMBOL(proc_irda);

static struct irda_entry irda_dirs[] = {
	{"discovery",	&discovery_seq_fops},
	{"irttp",	&irttp_seq_fops},
	{"irlmp",	&irlmp_seq_fops},
	{"irlap",	&irlap_seq_fops},
	{"irias",	&irias_seq_fops},
};

/*
 * Function irda_proc_register (void)
 *
 *    Register irda entry in /proc file system
 *
 */
void __init irda_proc_register(void)
{
	int i;
	struct proc_dir_entry *d;

	proc_irda = proc_mkdir("irda", proc_net);
	if (proc_irda == NULL)
		return;
	proc_irda->owner = THIS_MODULE;

	for (i=0; i<ARRAY_SIZE(irda_dirs); i++) {
		d = create_proc_entry(irda_dirs[i].name, 0, proc_irda);
		if (d)
			d->proc_fops = irda_dirs[i].fops;
	}
}

/*
 * Function irda_proc_unregister (void)
 *
 *    Unregister irda entry in /proc file system
 *
 */
void __exit irda_proc_unregister(void)
{
	int i;

	if (proc_irda) {
		for (i=0; i<ARRAY_SIZE(irda_dirs); i++)
			remove_proc_entry(irda_dirs[i].name, proc_irda);

		remove_proc_entry("irda", proc_net);
		proc_irda = NULL;
	}
}


