// SPDX-License-Identifier: LGPL-2.1
/*
 *   CIFS filesystem cache index structure definitions
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Authors(s): Suresh Jayaraman (sjayaraman@suse.de>
 *
 */
#include "fscache.h"
#include "cifs_debug.h"

/*
 * CIFS filesystem definition for FS-Cache
 */
struct fscache_netfs cifs_fscache_netfs = {
	.name = "cifs",
	.version = 0,
};

/*
 * Register CIFS for caching with FS-Cache
 */
int cifs_fscache_register(void)
{
	return fscache_register_netfs(&cifs_fscache_netfs);
}

/*
 * Unregister CIFS for caching
 */
void cifs_fscache_unregister(void)
{
	fscache_unregister_netfs(&cifs_fscache_netfs);
}

/*
 * Server object for FS-Cache
 */
const struct fscache_cookie_def cifs_fscache_server_index_def = {
	.name = "CIFS.server",
	.type = FSCACHE_COOKIE_TYPE_INDEX,
};

static enum
fscache_checkaux cifs_fscache_super_check_aux(void *cookie_netfs_data,
					      const void *data,
					      uint16_t datalen,
					      loff_t object_size)
{
	struct cifs_fscache_super_auxdata auxdata;
	const struct cifs_tcon *tcon = cookie_netfs_data;

	if (datalen != sizeof(auxdata))
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.resource_id = tcon->resource_id;
	auxdata.vol_create_time = tcon->vol_create_time;
	auxdata.vol_serial_number = tcon->vol_serial_number;

	if (memcmp(data, &auxdata, datalen) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

/*
 * Superblock object for FS-Cache
 */
const struct fscache_cookie_def cifs_fscache_super_index_def = {
	.name = "CIFS.super",
	.type = FSCACHE_COOKIE_TYPE_INDEX,
	.check_aux = cifs_fscache_super_check_aux,
};

static enum
fscache_checkaux cifs_fscache_inode_check_aux(void *cookie_netfs_data,
					      const void *data,
					      uint16_t datalen,
					      loff_t object_size)
{
	struct cifs_fscache_inode_auxdata auxdata;
	struct cifsInodeInfo *cifsi = cookie_netfs_data;

	if (datalen != sizeof(auxdata))
		return FSCACHE_CHECKAUX_OBSOLETE;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.eof = cifsi->server_eof;
	auxdata.last_write_time_sec = cifsi->vfs_inode.i_mtime.tv_sec;
	auxdata.last_change_time_sec = cifsi->vfs_inode.i_ctime.tv_sec;
	auxdata.last_write_time_nsec = cifsi->vfs_inode.i_mtime.tv_nsec;
	auxdata.last_change_time_nsec = cifsi->vfs_inode.i_ctime.tv_nsec;

	if (memcmp(data, &auxdata, datalen) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

const struct fscache_cookie_def cifs_fscache_inode_object_def = {
	.name		= "CIFS.uniqueid",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= cifs_fscache_inode_check_aux,
};
