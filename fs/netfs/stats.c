// SPDX-License-Identifier: GPL-2.0-or-later
/* Netfs support statistics
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/seq_file.h>
#include "internal.h"

atomic_t netfs_n_rh_dio_read;
atomic_t netfs_n_rh_readahead;
atomic_t netfs_n_rh_read_folio;
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
atomic_t netfs_n_rh_write_begin;
atomic_t netfs_n_rh_write_done;
atomic_t netfs_n_rh_write_failed;
atomic_t netfs_n_rh_write_zskip;
atomic_t netfs_n_wh_buffered_write;
atomic_t netfs_n_wh_writethrough;
atomic_t netfs_n_wh_dio_write;
atomic_t netfs_n_wh_writepages;
atomic_t netfs_n_wh_wstream_conflict;
atomic_t netfs_n_wh_upload;
atomic_t netfs_n_wh_upload_done;
atomic_t netfs_n_wh_upload_failed;
atomic_t netfs_n_wh_write;
atomic_t netfs_n_wh_write_done;
atomic_t netfs_n_wh_write_failed;

int netfs_stats_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Netfs  : DR=%u RA=%u RF=%u WB=%u WBZ=%u\n",
		   atomic_read(&netfs_n_rh_dio_read),
		   atomic_read(&netfs_n_rh_readahead),
		   atomic_read(&netfs_n_rh_read_folio),
		   atomic_read(&netfs_n_rh_write_begin),
		   atomic_read(&netfs_n_rh_write_zskip));
	seq_printf(m, "Netfs  : BW=%u WT=%u DW=%u WP=%u\n",
		   atomic_read(&netfs_n_wh_buffered_write),
		   atomic_read(&netfs_n_wh_writethrough),
		   atomic_read(&netfs_n_wh_dio_write),
		   atomic_read(&netfs_n_wh_writepages));
	seq_printf(m, "Netfs  : ZR=%u sh=%u sk=%u\n",
		   atomic_read(&netfs_n_rh_zero),
		   atomic_read(&netfs_n_rh_short_read),
		   atomic_read(&netfs_n_rh_write_zskip));
	seq_printf(m, "Netfs  : DL=%u ds=%u df=%u di=%u\n",
		   atomic_read(&netfs_n_rh_download),
		   atomic_read(&netfs_n_rh_download_done),
		   atomic_read(&netfs_n_rh_download_failed),
		   atomic_read(&netfs_n_rh_download_instead));
	seq_printf(m, "Netfs  : RD=%u rs=%u rf=%u\n",
		   atomic_read(&netfs_n_rh_read),
		   atomic_read(&netfs_n_rh_read_done),
		   atomic_read(&netfs_n_rh_read_failed));
	seq_printf(m, "Netfs  : UL=%u us=%u uf=%u\n",
		   atomic_read(&netfs_n_wh_upload),
		   atomic_read(&netfs_n_wh_upload_done),
		   atomic_read(&netfs_n_wh_upload_failed));
	seq_printf(m, "Netfs  : WR=%u ws=%u wf=%u\n",
		   atomic_read(&netfs_n_wh_write),
		   atomic_read(&netfs_n_wh_write_done),
		   atomic_read(&netfs_n_wh_write_failed));
	seq_printf(m, "Netfs  : rr=%u sr=%u wsc=%u\n",
		   atomic_read(&netfs_n_rh_rreq),
		   atomic_read(&netfs_n_rh_sreq),
		   atomic_read(&netfs_n_wh_wstream_conflict));
	return fscache_stats_show(m);
}
EXPORT_SYMBOL(netfs_stats_show);
