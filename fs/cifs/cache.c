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
 * Key layout of CIFS server cache index object
 */
struct cifs_server_key {
	uint16_t	family;		/* address family */
	__be16		port;		/* IP port */
	union {
		struct in_addr	ipv4_addr;
		struct in6_addr	ipv6_addr;
	} addr[0];
};

/*
 * Server object keyed by {IPaddress,port,family} tuple
 */
static uint16_t cifs_server_get_key(const void *cookie_netfs_data,
				   void *buffer, uint16_t maxbuf)
{
	const struct TCP_Server_Info *server = cookie_netfs_data;
	const struct sockaddr *sa = (struct sockaddr *) &server->dstaddr;
	const struct sockaddr_in *addr = (struct sockaddr_in *) sa;
	const struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *) sa;
	struct cifs_server_key *key = buffer;
	uint16_t key_len = sizeof(struct cifs_server_key);

	memset(key, 0, key_len);

	/*
	 * Should not be a problem as sin_family/sin6_family overlays
	 * sa_family field
	 */
	switch (sa->sa_family) {
	case AF_INET:
		key->family = sa->sa_family;
		key->port = addr->sin_port;
		key->addr[0].ipv4_addr = addr->sin_addr;
		key_len += sizeof(key->addr[0].ipv4_addr);
		break;

	case AF_INET6:
		key->family = sa->sa_family;
		key->port = addr6->sin6_port;
		key->addr[0].ipv6_addr = addr6->sin6_addr;
		key_len += sizeof(key->addr[0].ipv6_addr);
		break;

	default:
		cifs_dbg(VFS, "Unknown network family '%d'\n", sa->sa_family);
		key_len = 0;
		break;
	}

	return key_len;
}

/*
 * Server object for FS-Cache
 */
const struct fscache_cookie_def cifs_fscache_server_index_def = {
	.name = "CIFS.server",
	.type = FSCACHE_COOKIE_TYPE_INDEX,
	.get_key = cifs_server_get_key,
};

/*
 * Auxiliary data attached to CIFS superblock within the cache
 */
struct cifs_fscache_super_auxdata {
	u64	resource_id;		/* unique server resource id */
};

static char *extract_sharename(const char *treename)
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

/*
 * Superblock object currently keyed by share name
 */
static uint16_t cifs_super_get_key(const void *cookie_netfs_data, void *buffer,
				   uint16_t maxbuf)
{
	const struct cifs_tcon *tcon = cookie_netfs_data;
	char *sharename;
	uint16_t len;

	sharename = extract_sharename(tcon->treeName);
	if (IS_ERR(sharename)) {
		cifs_dbg(FYI, "%s: couldn't extract sharename\n", __func__);
		sharename = NULL;
		return 0;
	}

	len = strlen(sharename);
	if (len > maxbuf)
		return 0;

	memcpy(buffer, sharename, len);

	kfree(sharename);

	return len;
}

static uint16_t
cifs_fscache_super_get_aux(const void *cookie_netfs_data, void *buffer,
			   uint16_t maxbuf)
{
	struct cifs_fscache_super_auxdata auxdata;
	const struct cifs_tcon *tcon = cookie_netfs_data;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.resource_id = tcon->resource_id;

	if (maxbuf > sizeof(auxdata))
		maxbuf = sizeof(auxdata);

	memcpy(buffer, &auxdata, maxbuf);

	return maxbuf;
}

static enum
fscache_checkaux cifs_fscache_super_check_aux(void *cookie_netfs_data,
					      const void *data,
					      uint16_t datalen)
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
	.get_key = cifs_super_get_key,
	.get_aux = cifs_fscache_super_get_aux,
	.check_aux = cifs_fscache_super_check_aux,
};

/*
 * Auxiliary data attached to CIFS inode within the cache
 */
struct cifs_fscache_inode_auxdata {
	struct timespec	last_write_time;
	struct timespec	last_change_time;
	u64		eof;
};

static uint16_t cifs_fscache_inode_get_key(const void *cookie_netfs_data,
					   void *buffer, uint16_t maxbuf)
{
	const struct cifsInodeInfo *cifsi = cookie_netfs_data;
	uint16_t keylen;

	/* use the UniqueId as the key */
	keylen = sizeof(cifsi->uniqueid);
	if (keylen > maxbuf)
		keylen = 0;
	else
		memcpy(buffer, &cifsi->uniqueid, keylen);

	return keylen;
}

static void
cifs_fscache_inode_get_attr(const void *cookie_netfs_data, uint64_t *size)
{
	const struct cifsInodeInfo *cifsi = cookie_netfs_data;

	*size = cifsi->vfs_inode.i_size;
}

static uint16_t
cifs_fscache_inode_get_aux(const void *cookie_netfs_data, void *buffer,
			   uint16_t maxbuf)
{
	struct cifs_fscache_inode_auxdata auxdata;
	const struct cifsInodeInfo *cifsi = cookie_netfs_data;

	memset(&auxdata, 0, sizeof(auxdata));
	auxdata.eof = cifsi->server_eof;
	auxdata.last_write_time = cifsi->vfs_inode.i_mtime;
	auxdata.last_change_time = cifsi->vfs_inode.i_ctime;

	if (maxbuf > sizeof(auxdata))
		maxbuf = sizeof(auxdata);

	memcpy(buffer, &auxdata, maxbuf);

	return maxbuf;
}

static enum
fscache_checkaux cifs_fscache_inode_check_aux(void *cookie_netfs_data,
					      const void *data,
					      uint16_t datalen)
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
	.get_key	= cifs_fscache_inode_get_key,
	.get_attr	= cifs_fscache_inode_get_attr,
	.get_aux	= cifs_fscache_inode_get_aux,
	.check_aux	= cifs_fscache_inode_check_aux,
};
