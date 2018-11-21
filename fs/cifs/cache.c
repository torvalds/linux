/*
 *   fs/cifs/cache.c - CIFS filesystem cache index structure definitions
 *
 *   Copyright (c) 2010 Novell, Inc.
 *   Authors(s): Suresh Jayaraman (sjayaraman@suse.de>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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

/*
 * Auxiliary data attached to CIFS superblock within the cache
 */
struct cifs_fscache_super_auxdata {
	u64	resource_id;		/* unique server resource id */
};

char *extract_sharename(const char *treename)
{
	const char *src;
	char *delim, *dst;
	int len;

	/* skip double chars at the beginning */
	src = treename + 2;

	/* share name is always preceded by '\\' now */
	delim = strchr(src, '\\');
	if (!delim)
		return ERR_PTR(-EINVAL);
	delim++;
	len = strlen(delim);

	/* caller has to free the memory */
	dst = kstrndup(delim, len, GFP_KERNEL);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	return dst;
}

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
	auxdata.last_write_time = cifsi->vfs_inode.i_mtime;
	auxdata.last_change_time = cifsi->vfs_inode.i_ctime;

	if (memcmp(data, &auxdata, datalen) != 0)
		return FSCACHE_CHECKAUX_OBSOLETE;

	return FSCACHE_CHECKAUX_OKAY;
}

const struct fscache_cookie_def cifs_fscache_inode_object_def = {
	.name		= "CIFS.uniqueid",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= cifs_fscache_inode_check_aux,
};
