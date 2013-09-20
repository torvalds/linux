/* Filesystem index definition
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include "internal.h"

static uint16_t fscache_fsdef_netfs_get_key(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax);

static uint16_t fscache_fsdef_netfs_get_aux(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax);

static
enum fscache_checkaux fscache_fsdef_netfs_check_aux(void *cookie_netfs_data,
						    const void *data,
						    uint16_t datalen);

/*
 * The root index is owned by FS-Cache itself.
 *
 * When a netfs requests caching facilities, FS-Cache will, if one doesn't
 * already exist, create an entry in the root index with the key being the name
 * of the netfs ("AFS" for example), and the auxiliary data holding the index
 * structure version supplied by the netfs:
 *
 *				     FSDEF
 *				       |
 *				 +-----------+
 *				 |           |
 *				NFS         AFS
 *			       [v=1]       [v=1]
 *
 * If an entry with the appropriate name does already exist, the version is
 * compared.  If the version is different, the entire subtree from that entry
 * will be discarded and a new entry created.
 *
 * The new entry will be an index, and a cookie referring to it will be passed
 * to the netfs.  This is then the root handle by which the netfs accesses the
 * cache.  It can create whatever objects it likes in that index, including
 * further indices.
 */
static struct fscache_cookie_def fscache_fsdef_index_def = {
	.name		= ".FS-Cache",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

struct fscache_cookie fscache_fsdef_index = {
	.usage		= ATOMIC_INIT(1),
	.n_active	= ATOMIC_INIT(1),
	.lock		= __SPIN_LOCK_UNLOCKED(fscache_fsdef_index.lock),
	.backing_objects = HLIST_HEAD_INIT,
	.def		= &fscache_fsdef_index_def,
	.flags		= 1 << FSCACHE_COOKIE_ENABLED,
};
EXPORT_SYMBOL(fscache_fsdef_index);

/*
 * Definition of an entry in the root index.  Each entry is an index, keyed to
 * a specific netfs and only applicable to a particular version of the index
 * structure used by that netfs.
 */
struct fscache_cookie_def fscache_fsdef_netfs_def = {
	.name		= "FSDEF.netfs",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= fscache_fsdef_netfs_get_key,
	.get_aux	= fscache_fsdef_netfs_get_aux,
	.check_aux	= fscache_fsdef_netfs_check_aux,
};

/*
 * get the key data for an FSDEF index record - this is the name of the netfs
 * for which this entry is created
 */
static uint16_t fscache_fsdef_netfs_get_key(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax)
{
	const struct fscache_netfs *netfs = cookie_netfs_data;
	unsigned klen;

	_enter("{%s.%u},", netfs->name, netfs->version);

	klen = strlen(netfs->name);
	if (klen > bufmax)
		return 0;

	memcpy(buffer, netfs->name, klen);
	return klen;
}

/*
 * get the auxiliary data for an FSDEF index record - this is the index
 * structure version number of the netfs for which this version is created
 */
static uint16_t fscache_fsdef_netfs_get_aux(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax)
{
	const struct fscache_netfs *netfs = cookie_netfs_data;
	unsigned dlen;

	_enter("{%s.%u},", netfs->name, netfs->version);

	dlen = sizeof(uint32_t);
	if (dlen > bufmax)
		return 0;

	memcpy(buffer, &netfs->version, dlen);
	return dlen;
}

/*
 * check that the index structure version number stored in the auxiliary data
 * matches the one the netfs gave us
 */
static enum fscache_checkaux fscache_fsdef_netfs_check_aux(
	void *cookie_netfs_data,
	const void *data,
	uint16_t datalen)
{
	struct fscache_netfs *netfs = cookie_netfs_data;
	uint32_t version;

	_enter("{%s},,%hu", netfs->name, datalen);

	if (datalen != sizeof(version)) {
		_leave(" = OBSOLETE [dl=%d v=%zu]", datalen, sizeof(version));
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	memcpy(&version, data, sizeof(version));
	if (version != netfs->version) {
		_leave(" = OBSOLETE [ver=%x net=%x]", version, netfs->version);
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	_leave(" = OKAY");
	return FSCACHE_CHECKAUX_OKAY;
}
