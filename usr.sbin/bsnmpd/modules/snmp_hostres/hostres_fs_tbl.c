/*-
 * Copyright (c) 2005-2006 The FreeBSD Project
 * All rights reserved.
 *
 * Author: Victor Cruceru <soc-victor@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Host Resources MIB for SNMPd. Implementation for hrFSTable
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/mount.h>

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sysexits.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

/*
 * File system access enum
 */
enum hrFSAccess {
	FS_READ_WRITE = 1,
	FS_READ_ONLY  = 2
};

/* maximum length (according to MIB) for fs_entry::mountPoint */
#define	FS_MP_MLEN	(128 + 1)

/* maximum length (according to MIB) for fs_entry::remoteMountPoint */
#define	FS_RMP_MLEN	(128 + 1)

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrFSTable
 */
struct fs_entry {
	int32_t		index;
	u_char		*mountPoint;
	u_char		*remoteMountPoint;
	const struct asn_oid *type;
	int32_t		access;		/* enum hrFSAccess, see above */
	int32_t		bootable;	/* TruthValue */
	int32_t		storageIndex;	/* hrStorageTblEntry::index */
	u_char		lastFullBackupDate[11];
	u_char		lastPartialBackupDate[11];
#define	HR_FS_FOUND 0x001
	uint32_t	flags;		/* not in mib table, for internal use */
	TAILQ_ENTRY(fs_entry) link;
};
TAILQ_HEAD(fs_tbl, fs_entry);

/*
 * Next structure is used to keep o list of mappings from a specific name
 * (a_name) to an entry in the hrFSTblEntry. We are trying to keep the same
 * index for a specific name at least for the duration of one SNMP agent run.
 */
struct fs_map_entry {
	int32_t		hrIndex;   /* used for fs_entry::index */
	u_char		*a_name;   /* map key same as fs_entry::mountPoint */

	/* may be NULL if the respective hrFSTblEntry is (temporally) gone */
	struct fs_entry *entry;
	STAILQ_ENTRY(fs_map_entry) 	link;
};
STAILQ_HEAD(fs_map, fs_map_entry);

/* head of the list with hrFSTable's entries */
static struct fs_tbl fs_tbl = TAILQ_HEAD_INITIALIZER(fs_tbl);

/* for consistent table indexing */
static struct fs_map fs_map = STAILQ_HEAD_INITIALIZER(fs_map);

/* next index available for hrFSTable */
static uint32_t	next_fs_index = 1;

/* last tick when hrFSTable was updated */
static uint64_t fs_tick;

/* maximum number of ticks between refreshs */
uint32_t fs_tbl_refresh = HR_FS_TBL_REFRESH * 100;

/* some constants */
static const struct asn_oid OIDX_hrFSBerkeleyFFS_c = OIDX_hrFSBerkeleyFFS;
static const struct asn_oid OIDX_hrFSiso9660_c = OIDX_hrFSiso9660;
static const struct asn_oid OIDX_hrFSNFS_c = OIDX_hrFSNFS;
static const struct asn_oid OIDX_hrFSLinuxExt2_c = OIDX_hrFSLinuxExt2;
static const struct asn_oid OIDX_hrFSOther_c = OIDX_hrFSOther;
static const struct asn_oid OIDX_hrFSFAT32_c = OIDX_hrFSFAT32;
static const struct asn_oid OIDX_hrFSNTFS_c = OIDX_hrFSNTFS;
static const struct asn_oid OIDX_hrFSNetware_c = OIDX_hrFSNetware;
static const struct asn_oid OIDX_hrFSHPFS_c = OIDX_hrFSHPFS;
static const struct asn_oid OIDX_hrFSUnknown_c = OIDX_hrFSUnknown;

/* file system type map */
static const struct {
	const char		*str;	/* the type string */
	const struct asn_oid	*oid;	/* the OID to return */
} fs_type_map[] = {
	{ "ufs",	&OIDX_hrFSBerkeleyFFS_c },
	{ "zfs",	&OIDX_hrFSOther_c },
	{ "cd9660",	&OIDX_hrFSiso9660_c },
	{ "nfs",	&OIDX_hrFSNFS_c },
	{ "ext2fs",	&OIDX_hrFSLinuxExt2_c },
	{ "procfs",	&OIDX_hrFSOther_c },
	{ "devfs",	&OIDX_hrFSOther_c },
	{ "msdosfs",	&OIDX_hrFSFAT32_c },
	{ "ntfs",	&OIDX_hrFSNTFS_c },
	{ "nwfs",	&OIDX_hrFSNetware_c },
	{ "hpfs",	&OIDX_hrFSHPFS_c },
	{ "smbfs",	&OIDX_hrFSOther_c },
};
#define	N_FS_TYPE_MAP	nitems(fs_type_map)

/**
 * Create an entry into the FS table and an entry in the map (if needed).
 */
static struct fs_entry *
fs_entry_create(const char *name)
{
	struct fs_entry	*entry;
	struct fs_map_entry *map;

	assert(name != NULL);
	assert(strlen(name) > 0);

	STAILQ_FOREACH(map, &fs_map, link)
		if (strcmp(map->a_name, name) == 0)
			break;

	if (map == NULL) {
		size_t mount_point_len;

		/* new object - get a new index */
		if (next_fs_index > INT_MAX) {
			/* Unrecoverable error - die clean and quicly*/
			syslog(LOG_ERR, "%s: hrFSTable index wrap", __func__);
			errx(EX_SOFTWARE, "hrFSTable index wrap");
		}

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "%s: %m", __func__);
			return (NULL);
		}

		mount_point_len = strlen(name) + 1;
		if (mount_point_len > FS_MP_MLEN)
			mount_point_len = FS_MP_MLEN;

		if ((map->a_name = malloc(mount_point_len)) == NULL) {
			syslog(LOG_ERR, "%s: %m", __func__);
			free(map);
			return (NULL);
		}

		strlcpy(map->a_name, name, mount_point_len);

		map->hrIndex = next_fs_index++;
		map->entry = NULL;
		STAILQ_INSERT_TAIL(&fs_map, map, link);

		HRDBG("%s added into hrFSMap at index=%d", name, map->hrIndex);
	} else {
		HRDBG("%s exists in hrFSMap index=%d", name, map->hrIndex);
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}

	if ((entry->mountPoint = strdup(name)) == NULL) {
		syslog(LOG_ERR, "%s: %m", __func__);
		free(entry);
		return (NULL);
	}

	entry->index = map->hrIndex;
	map->entry = entry;

	INSERT_OBJECT_INT(entry, &fs_tbl);
	return (entry);
}

/**
 * Delete an entry in the FS table.
 */
static void
fs_entry_delete(struct fs_entry* entry)
{
	struct fs_map_entry *map;

	assert(entry != NULL);

	TAILQ_REMOVE(&fs_tbl, entry, link);
	STAILQ_FOREACH(map, &fs_map, link)
		if (map->entry == entry) {
			map->entry = NULL;
			break;
		}
	free(entry->mountPoint);
	free(entry->remoteMountPoint);
	free(entry);
}

/**
 * Find a table entry by its name
 */
static struct fs_entry *
fs_find_by_name(const char *name)
{
	struct fs_entry *entry;

	TAILQ_FOREACH(entry, &fs_tbl, link)
		if (strcmp(entry->mountPoint, name) == 0)
			return (entry);

	return (NULL);
}

/**
 * Get rid of all data
 */
void
fini_fs_tbl(void)
{
	struct fs_map_entry *n1;

     	while ((n1 = STAILQ_FIRST(&fs_map)) != NULL) {
		STAILQ_REMOVE_HEAD(&fs_map, link);
		if (n1->entry != NULL) {
			TAILQ_REMOVE(&fs_tbl, n1->entry, link);
			free(n1->entry->mountPoint);
			free(n1->entry->remoteMountPoint);
			free(n1->entry);
		}
		free(n1->a_name);
		free(n1);
     	}
	assert(TAILQ_EMPTY(&fs_tbl));
}

/**
 * Called before the refreshing is started from the storage table.
 */
void
fs_tbl_pre_refresh(void)
{
	struct fs_entry *entry;

	/* mark each entry as missisng */
	TAILQ_FOREACH(entry, &fs_tbl, link)
		entry->flags &= ~HR_FS_FOUND;
}

/**
 * Called after refreshing from the storage table.
 */
void
fs_tbl_post_refresh(void)
{
	struct fs_entry *entry, *entry_tmp;

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &fs_tbl, link, entry_tmp)
		if (!(entry->flags & HR_FS_FOUND))
			fs_entry_delete(entry);

	fs_tick = this_tick;
}

/*
 * Refresh the FS table. This is done by forcing a refresh of the storage table.
 */
void
refresh_fs_tbl(void)
{

	if (fs_tick == 0 || this_tick - fs_tick >= fs_tbl_refresh) {
		refresh_storage_tbl(1);
		HRDBG("refresh DONE");
	}
}

/**
 * Get the type OID for a given file system
 */
const struct asn_oid *
fs_get_type(const struct statfs *fs_p)
{
	u_int t;

	assert(fs_p != NULL);

	for (t = 0; t < N_FS_TYPE_MAP; t++)
		if (strcmp(fs_type_map[t].str, fs_p->f_fstypename) == 0)
			return (fs_type_map[t].oid);

	return (&OIDX_hrFSUnknown_c);
}

/*
 * Given information returned from statfs(2) either create a new entry into
 * the fs_tbl or refresh the entry if it is already there.
 */
void
fs_tbl_process_statfs_entry(const struct statfs *fs_p, int32_t storage_idx)
{
	struct fs_entry *entry;

	assert(fs_p != 0);

	HRDBG("for hrStorageEntry::index %d", storage_idx);

	if (fs_p == NULL)
		return;

	if ((entry = fs_find_by_name(fs_p->f_mntonname)) != NULL ||
	    (entry = fs_entry_create(fs_p->f_mntonname)) != NULL) {
		entry->flags |= HR_FS_FOUND;

		if (!(fs_p->f_flags & MNT_LOCAL)) {
			/* this is a remote mount */
			entry->remoteMountPoint = strdup(fs_p->f_mntfromname);
			/* if strdup failed, let it be NULL */

		} else {
			entry->remoteMountPoint = strdup("");
			/* if strdup failed, let it be NULL */
		}

		entry->type = fs_get_type(fs_p);

		if ((fs_p->f_flags & MNT_RDONLY) == MNT_RDONLY)
			entry->access = FS_READ_ONLY;
		else
			entry->access = FS_READ_WRITE;

		/* FIXME - bootable fs ?! */
		entry->bootable = TRUTH_MK((fs_p->f_flags & MNT_ROOTFS)
		    == MNT_ROOTFS);

		entry->storageIndex = storage_idx;

		/* Info not available */
		memset(entry->lastFullBackupDate, 0,
		    sizeof(entry->lastFullBackupDate));

		/* Info not available */
		memset(entry->lastPartialBackupDate, 0,
		    sizeof(entry->lastPartialBackupDate));

		handle_partition_fs_index(fs_p->f_mntfromname, entry->index);
	}
}

/*
 * This is the implementation for a generated (by our SNMP "compiler" tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrFSTable
 */
int
op_hrFSTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct fs_entry *entry;

	refresh_fs_tbl();

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&fs_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&fs_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&fs_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NO_CREATION);
		return (SNMP_ERR_NOT_WRITEABLE);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		abort();
	}
	abort();
  get:
	switch (value->var.subs[sub - 1]) {

	case LEAF_hrFSIndex:
		value->v.integer = entry->index;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrFSMountPoint:
		return (string_get(value, entry->mountPoint, -1));

	case LEAF_hrFSRemoteMountPoint:
		if (entry->remoteMountPoint == NULL)
			return (string_get(value, "", -1));
		else
			return (string_get(value, entry->remoteMountPoint, -1));
		break;

	case LEAF_hrFSType:
		assert(entry->type != NULL);
		value->v.oid = *(entry->type);
		return (SNMP_ERR_NOERROR);

	case LEAF_hrFSAccess:
		value->v.integer = entry->access;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrFSBootable:
		value->v.integer = entry->bootable;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrFSStorageIndex:
		value->v.integer = entry->storageIndex;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrFSLastFullBackupDate:
		return (string_get(value, entry->lastFullBackupDate, 8));

	case LEAF_hrFSLastPartialBackupDate:
		return (string_get(value, entry->lastPartialBackupDate, 8));
	}
	abort();
}
