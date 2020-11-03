// SPDX-License-Identifier: GPL-2.0-or-later
/* Netfs support statistics
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/seq_file.h>
#include <linux/netfs.h>
#include "internal.h"

atomic_t netfs_n_rh_readahead;
atomic_t netfs_n_rh_readpage;
atomic_t netfs_n_rh_rreq;
atomic_t netfs_n_rh_sreq;
atomic_t netfs_n_rh_download;
atomic_t netfs_n_rh_download_done;
atomic_t netfs_n_rh_download_failed;
atomic_t netfs_n_rh_download_instead;
atomic_t netfs_n_rh_read;
atomic_t netfs_n_rh_read_done;
atomic_t netfs_n_rh_read_failed;
atomic_t netfs_n_rh_zero;
atomic_t netfs_n_rh_short_read;
atomic_t netfs_n_rh_write;
atomic_t netfs_n_rh_write_done;
atomic_t netfs_n_rh_write_failed;

void netfs_stats_show(struct seq_file *m)
{
	seq_printf(m, "RdHelp : RA=%u RP=%u rr=%u sr=%u\n",
		   atomic_read(&netfs_n_rh_readahead),
		   atomic_read(&netfs_n_rh_readpage),
		   atomic_read(&netfs_n_rh_rreq),
		   atomic_read(&netfs_n_rh_sreq));
	seq_printf(m, "RdHelp : ZR=%u sh=%u\n",
		   atomic_read(&netfs_n_rh_zero),
		   atomic_read(&netfs_n_rh_short_read));
	seq_printf(m, "RdHelp : DL=%u ds=%u df=%u di=%u\n",
		   atomic_read(&netfs_n_rh_download),
		   atomic_read(&netfs_n_rh_download_done),
		   atomic_read(&netfs_n_rh_download_failed),
		   atomic_read(&netfs_n_rh_download_instead));
	seq_printf(m, "RdHelp : RD=%u rs=%u rf=%u\n",
		   atomic_read(&netfs_n_rh_read),
		   atomic_read(&netfs_n_rh_read_done),
		   atomic_read(&netfs_n_rh_read_failed));
	seq_printf(m, "RdHelp : WR=%u ws=%u wf=%u\n",
		   atomic_read(&netfs_n_rh_write),
		   atomic_read(&netfs_n_rh_write_done),
		   atomic_read(&netfs_n_rh_write_failed));
}
EXPORT_SYMBOL(netfs_stats_show);
