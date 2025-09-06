// SPDX-License-Identifier: GPL-2.0

#include <linux/debugfs.h>

#include "nfsd.h"

static struct dentry *nfsd_top_dir __read_mostly;

/*
 * /sys/kernel/debug/nfsd/disable-splice-read
 *
 * Contents:
 *   %0: NFS READ is allowed to use page splicing
 *   %1: NFS READ uses only iov iter read
 *
 * The default value of this setting is zero (page splicing is
 * allowed). This setting takes immediate effect for all NFS
 * versions, all exports, and in all NFSD net namespaces.
 */

static int nfsd_dsr_get(void *data, u64 *val)
{
	*val = nfsd_disable_splice_read ? 1 : 0;
	return 0;
}

static int nfsd_dsr_set(void *data, u64 val)
{
	nfsd_disable_splice_read = (val > 0);
	if (!nfsd_disable_splice_read) {
		/*
		 * Must use buffered I/O if splice_read is enabled.
		 */
		nfsd_io_cache_read = NFSD_IO_BUFFERED;
	}
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(nfsd_dsr_fops, nfsd_dsr_get, nfsd_dsr_set, "%llu\n");

/*
 * /sys/kernel/debug/nfsd/io_cache_read
 *
 * Contents:
 *   %0: NFS READ will use buffered IO
 *   %1: NFS READ will use dontcache (buffered IO w/ dropbehind)
 *
 * This setting takes immediate effect for all NFS versions,
 * all exports, and in all NFSD net namespaces.
 */

static int nfsd_io_cache_read_get(void *data, u64 *val)
{
	*val = nfsd_io_cache_read;
	return 0;
}

static int nfsd_io_cache_read_set(void *data, u64 val)
{
	int ret = 0;

	switch (val) {
	case NFSD_IO_BUFFERED:
		nfsd_io_cache_read = NFSD_IO_BUFFERED;
		break;
	case NFSD_IO_DONTCACHE:
		/*
		 * Must disable splice_read when enabling
		 * NFSD_IO_DONTCACHE.
		 */
		nfsd_disable_splice_read = true;
		nfsd_io_cache_read = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(nfsd_io_cache_read_fops, nfsd_io_cache_read_get,
			 nfsd_io_cache_read_set, "%llu\n");

/*
 * /sys/kernel/debug/nfsd/io_cache_write
 *
 * Contents:
 *   %0: NFS WRITE will use buffered IO
 *   %1: NFS WRITE will use dontcache (buffered IO w/ dropbehind)
 *
 * This setting takes immediate effect for all NFS versions,
 * all exports, and in all NFSD net namespaces.
 */

static int nfsd_io_cache_write_get(void *data, u64 *val)
{
	*val = nfsd_io_cache_write;
	return 0;
}

static int nfsd_io_cache_write_set(void *data, u64 val)
{
	int ret = 0;

	switch (val) {
	case NFSD_IO_BUFFERED:
	case NFSD_IO_DONTCACHE:
		nfsd_io_cache_write = val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(nfsd_io_cache_write_fops, nfsd_io_cache_write_get,
			 nfsd_io_cache_write_set, "%llu\n");

void nfsd_debugfs_exit(void)
{
	debugfs_remove_recursive(nfsd_top_dir);
	nfsd_top_dir = NULL;
}

void nfsd_debugfs_init(void)
{
	nfsd_top_dir = debugfs_create_dir("nfsd", NULL);

	debugfs_create_file("disable-splice-read", S_IWUSR | S_IRUGO,
			    nfsd_top_dir, NULL, &nfsd_dsr_fops);

	debugfs_create_file("io_cache_read", 0644, nfsd_top_dir, NULL,
			    &nfsd_io_cache_read_fops);

	debugfs_create_file("io_cache_write", 0644, nfsd_top_dir, NULL,
			    &nfsd_io_cache_write_fops);
}
