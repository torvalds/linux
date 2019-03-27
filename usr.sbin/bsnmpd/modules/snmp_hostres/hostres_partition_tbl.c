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
 * Host Resources MIB: hrPartitionTable implementation for SNMPd.
 */

#include <sys/types.h>
#include <sys/limits.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <libgeom.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sysexits.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

#define	HR_FREEBSD_PART_TYPE	165

/* Maximum length for label and id including \0 */
#define	PART_STR_MLEN	(128 + 1)

/*
 * One row in the hrPartitionTable
 */
struct partition_entry {
	asn_subid_t	index[2];
	u_char		*label;	/* max allocated len will be PART_STR_MLEN */
	u_char		*id;	/* max allocated len will be PART_STR_MLEN */
	int32_t		size;
	int32_t		fs_Index;
	TAILQ_ENTRY(partition_entry) link;
#define	HR_PARTITION_FOUND		0x001
	uint32_t	flags;
};
TAILQ_HEAD(partition_tbl, partition_entry);

/*
 * This table is used to get a consistent indexing. It saves the name -> index
 * mapping while we rebuild the partition table.
 */
struct partition_map_entry {
	int32_t		index;	/* partition_entry::index */
	u_char		*id;	/* max allocated len will be PART_STR_MLEN */

	/*
	 * next may be NULL if the respective partition_entry
	 * is (temporally) gone.
	 */
	struct partition_entry	*entry;
	STAILQ_ENTRY(partition_map_entry) link;
};
STAILQ_HEAD(partition_map, partition_map_entry);

/* Mapping table for consistent indexing */
static struct partition_map partition_map =
    STAILQ_HEAD_INITIALIZER(partition_map);

/* THE partition table. */
static struct partition_tbl partition_tbl =
    TAILQ_HEAD_INITIALIZER(partition_tbl);

/* next int available for indexing the hrPartitionTable */
static uint32_t next_partition_index = 1;

/*
 * Partition_entry_cmp is used for INSERT_OBJECT_FUNC_LINK
 * macro.
 */
static int
partition_entry_cmp(const struct partition_entry *a,
    const struct partition_entry *b)
{
	assert(a != NULL);
	assert(b != NULL);

	if (a->index[0] < b->index[0])
		return (-1);

	if (a->index[0] > b->index[0])
		return (+1);

	if (a->index[1] < b->index[1])
		return (-1);

	if (a->index[1] > b->index[1])
		return (+1);

	return (0);
}

/*
 * Partition_idx_cmp is used for NEXT_OBJECT_FUNC and FIND_OBJECT_FUNC
 * macros
 */
static int
partition_idx_cmp(const struct asn_oid *oid, u_int sub,
    const struct partition_entry *entry)
{
	u_int i;

	for (i = 0; i < 2 && i < oid->len - sub; i++) {
		if (oid->subs[sub + i] < entry->index[i])
			return (-1);
		if (oid->subs[sub + i] > entry->index[i])
			return (+1);
	}
	if (oid->len - sub < 2)
		return (-1);
	if (oid->len - sub > 2)
		return (+1);

	return (0);
}

/**
 * Create a new partition table entry
 */
static struct partition_entry *
partition_entry_create(int32_t ds_index, const char *chunk_name)
{
	struct partition_entry *entry;
	struct partition_map_entry *map;
	size_t id_len;

	/* sanity checks */
	assert(chunk_name != NULL);
	if (chunk_name == NULL || chunk_name[0] == '\0')
		return (NULL);

	/* check whether we already have seen this partition */
	STAILQ_FOREACH(map, &partition_map, link)
		if (strcmp(map->id, chunk_name) == 0)
			break;

	if (map == NULL) {
		/* new object - get a new index and create a map */

		if (next_partition_index > INT_MAX) {
			/* Unrecoverable error - die clean and quicly*/
			syslog(LOG_ERR, "%s: hrPartitionTable index wrap",
			    __func__);
			errx(EX_SOFTWARE, "hrPartitionTable index wrap");
		}

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "hrPartitionTable: %s: %m", __func__);
			return (NULL);
		}

		id_len = strlen(chunk_name) + 1;
		if (id_len > PART_STR_MLEN)
			id_len = PART_STR_MLEN;

		if ((map->id = malloc(id_len)) == NULL) {
			free(map);
			return (NULL);
		}

		map->index = next_partition_index++;

		strlcpy(map->id, chunk_name, id_len);

		map->entry = NULL;

		STAILQ_INSERT_TAIL(&partition_map, map, link);

		HRDBG("%s added into hrPartitionMap at index=%d",
		    chunk_name, map->index);

	} else {
		HRDBG("%s exists in hrPartitionMap index=%d",
		    chunk_name, map->index);
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "hrPartitionTable: %s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));

	/* create the index */
	entry->index[0] = ds_index;
	entry->index[1] = map->index;

	map->entry = entry;

	if ((entry->id = strdup(map->id)) == NULL) {
		free(entry);
		return (NULL);
	}

	/*
	 * reuse id_len from here till the end of this function
	 * for partition_entry::label
	 */
	id_len = strlen(_PATH_DEV) + strlen(chunk_name) + 1;

	if (id_len > PART_STR_MLEN)
		id_len = PART_STR_MLEN;

	if ((entry->label = malloc(id_len )) == NULL) {
		free(entry->id);
		free(entry);
		return (NULL);
	}

	snprintf(entry->label, id_len, "%s%s", _PATH_DEV, chunk_name);

	INSERT_OBJECT_FUNC_LINK(entry, &partition_tbl, link,
	    partition_entry_cmp);

	return (entry);
}

/**
 * Delete a partition table entry but keep the map entry intact.
 */
static void
partition_entry_delete(struct partition_entry *entry)
{
	struct partition_map_entry *map;

	assert(entry != NULL);

	TAILQ_REMOVE(&partition_tbl, entry, link);
	STAILQ_FOREACH(map, &partition_map, link)
		if (map->entry == entry) {
			map->entry = NULL;
			break;
		}
	free(entry->id);
	free(entry->label);
	free(entry);
}

/**
 * Find a partition table entry by name. If none is found, return NULL.
 */
static struct partition_entry *
partition_entry_find_by_name(const char *name)
{
	struct partition_entry *entry =  NULL;

	TAILQ_FOREACH(entry, &partition_tbl, link)
		if (strcmp(entry->id, name) == 0)
			return (entry);

	return (NULL);
}

/**
 * Find a partition table entry by label. If none is found, return NULL.
 */
static struct partition_entry *
partition_entry_find_by_label(const char *name)
{
	struct partition_entry *entry =  NULL;

	TAILQ_FOREACH(entry, &partition_tbl, link)
		if (strcmp(entry->label, name) == 0)
			return (entry);

	return (NULL);
}

/**
 * Process a chunk from libgeom(4). A chunk is either a slice or a partition.
 * If necessary create a new partition table entry for it. In any case
 * set the size field of the entry and set the FOUND flag.
 */
static void
handle_chunk(int32_t ds_index, const char *chunk_name, off_t chunk_size)
{
	struct partition_entry *entry;
	daddr_t k_size;

	assert(chunk_name != NULL);
	assert(chunk_name[0] != '\0');
	if (chunk_name == NULL || chunk_name[0] == '\0')
		return;

	HRDBG("ANALYZE chunk %s", chunk_name);

	if ((entry = partition_entry_find_by_name(chunk_name)) == NULL)
		if ((entry = partition_entry_create(ds_index,
		    chunk_name)) == NULL)
			return;

	entry->flags |= HR_PARTITION_FOUND;

	/* actual size may overflow the SNMP type */
	k_size = chunk_size / 1024;
	entry->size = (k_size > (off_t)INT_MAX ? INT_MAX : k_size);
}

/**
 * Start refreshing the partition table. A call to this function will
 * be followed by a call to handleDiskStorage() for every disk, followed
 * by a single call to the post_refresh function.
 */
void
partition_tbl_pre_refresh(void)
{
	struct partition_entry *entry;

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &partition_tbl, link)
		entry->flags &= ~HR_PARTITION_FOUND;
}

/**
 * Try to find a geom(4) class by its name. Returns a pointer to that
 * class if found NULL otherways.
 */
static struct gclass *
find_class(struct gmesh *mesh, const char *name)
{
	struct gclass *classp;

	LIST_FOREACH(classp, &mesh->lg_class, lg_class)
		if (strcmp(classp->lg_name, name) == 0)
			return (classp);
	return (NULL);
}

/**
 * Process all MBR-type partitions from the given disk.
 */
static void
get_mbr(struct gclass *classp, int32_t ds_index, const char *disk_dev_name)
{
	struct ggeom *gp;
	struct gprovider *pp;
	struct gconfig *conf;
	long part_type;

	LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
		/* We are only interested in partitions from this disk */
		if (strcmp(gp->lg_name, disk_dev_name) != 0)
			continue;

		/*
		 * Find all the non-BSD providers (these are handled in get_bsd)
		 */
		LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
			LIST_FOREACH(conf, &pp->lg_config, lg_config) {
				if (conf->lg_name == NULL ||
				    conf->lg_val == NULL ||
				    strcmp(conf->lg_name, "type") != 0)
					continue;

				/*
				 * We are not interested in BSD partitions
				 * (ie ad0s1 is not interesting at this point).
				 * We'll take care of them in detail (slice
				 * by slice) in get_bsd.
				 */
				part_type = strtol(conf->lg_val, NULL, 10);
				if (part_type == HR_FREEBSD_PART_TYPE)
					break;
				HRDBG("-> MBR PROVIDER Name: %s", pp->lg_name);
				HRDBG("Mediasize: %jd",
				    (intmax_t)pp->lg_mediasize / 1024);
				HRDBG("Sectorsize: %u", pp->lg_sectorsize);
				HRDBG("Mode: %s", pp->lg_mode);
				HRDBG("CONFIG: %s: %s",
				    conf->lg_name, conf->lg_val);

				handle_chunk(ds_index, pp->lg_name,
				    pp->lg_mediasize);
			}
		}
	}
}

/**
 * Process all BSD-type partitions from the given disk.
 */
static void
get_bsd_sun(struct gclass *classp, int32_t ds_index, const char *disk_dev_name)
{
	struct ggeom *gp;
	struct gprovider *pp;

	LIST_FOREACH(gp, &classp->lg_geom, lg_geom) {
		/*
		 * We are only interested in those geoms starting with
		 * the disk_dev_name passed as parameter to this function.
		 */
		if (strncmp(gp->lg_name, disk_dev_name,
		    strlen(disk_dev_name)) != 0)
			continue;

		LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
			if (pp->lg_name == NULL)
				continue;
			handle_chunk(ds_index, pp->lg_name, pp->lg_mediasize);
		}
	}
}

/**
 * Called from the DiskStorage table for every row. Open the GEOM(4) framework
 * and process all the partitions in it.
 * ds_index is the index into the DiskStorage table.
 * This is done in two steps: for non BSD partitions the geom class "MBR" is
 * used, for our BSD slices the "BSD" geom class.
 */
void
partition_tbl_handle_disk(int32_t ds_index, const char *disk_dev_name)
{
	struct gmesh mesh;	/* GEOM userland tree */
	struct gclass *classp;
	int error;

	assert(disk_dev_name != NULL);
	assert(ds_index > 0);

	HRDBG("===> getting partitions for %s <===", disk_dev_name);

	/* try to construct the GEOM tree */
	if ((error = geom_gettree(&mesh)) != 0) {
		syslog(LOG_WARNING, "cannot get GEOM tree: %m");
		return;
	}

	/*
	 * First try the GEOM "MBR" class.
	 * This is needed for non-BSD slices (aka partitions)
	 * on PC architectures.
	 */
	if ((classp = find_class(&mesh, "MBR")) != NULL) {
		get_mbr(classp, ds_index, disk_dev_name);
	} else {
		HRDBG("cannot find \"MBR\" geom class");
	}

	/*
	 * Get the "BSD" GEOM class.
	 * Here we'll find all the info needed about the BSD slices.
	 */
	if ((classp = find_class(&mesh, "BSD")) != NULL) {
		get_bsd_sun(classp, ds_index, disk_dev_name);
	} else {
		/* no problem on sparc64 */
		HRDBG("cannot find \"BSD\" geom class");
	}

	/*
	 * Get the "SUN" GEOM class.
	 * Here we'll find all the info needed about the BSD slices.
	 */
	if ((classp = find_class(&mesh, "SUN")) != NULL) {
		get_bsd_sun(classp, ds_index, disk_dev_name);
	} else {
		/* no problem on i386 */
		HRDBG("cannot find \"SUN\" geom class");
	}

	geom_deletetree(&mesh);
}

/**
 * Finish refreshing the table.
 */
void
partition_tbl_post_refresh(void)
{
	struct partition_entry *e, *etmp;

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(e, &partition_tbl, link, etmp)
		if (!(e->flags & HR_PARTITION_FOUND))
			partition_entry_delete(e);
}

/*
 * Finalization routine for hrPartitionTable
 * It destroys the lists and frees any allocated heap memory
 */
void
fini_partition_tbl(void)
{
	struct partition_map_entry *m;

	while ((m = STAILQ_FIRST(&partition_map)) != NULL) {
		STAILQ_REMOVE_HEAD(&partition_map, link);
		if(m->entry != NULL) {
			TAILQ_REMOVE(&partition_tbl, m->entry, link);
			free(m->entry->id);
			free(m->entry->label);
			free(m->entry);
		}
		free(m->id);
		free(m);
	}
	assert(TAILQ_EMPTY(&partition_tbl));
}

/**
 * Called from the file system code to insert the file system table index
 * into the partition table entry. Note, that an partition table entry exists
 * only for local file systems.
 */
void
handle_partition_fs_index(const char *name, int32_t fs_idx)
{
	struct partition_entry *entry;

	if ((entry = partition_entry_find_by_label(name)) == NULL) {
		HRDBG("%s IS MISSING from hrPartitionTable", name);
		return;
	}
	HRDBG("%s [FS index = %d] IS in hrPartitionTable", name, fs_idx);
	entry->fs_Index = fs_idx;
}

/*
 * This is the implementation for a generated (by our SNMP tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrPartitionTable
 */
int
op_hrPartitionTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct partition_entry *entry;

	/*
	 * Refresh the disk storage table (which refreshes the partition
	 * table) if necessary.
	 */
	refresh_disk_storage_tbl(0);

	switch (op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_FUNC(&partition_tbl,
		    &value->var, sub, partition_idx_cmp)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);

		value->var.len = sub + 2;
		value->var.subs[sub] = entry->index[0];
		value->var.subs[sub + 1] = entry->index[1];

		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_FUNC(&partition_tbl,
		    &value->var, sub, partition_idx_cmp)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_FUNC(&partition_tbl,
		    &value->var, sub, partition_idx_cmp)) == NULL)
			return (SNMP_ERR_NOT_WRITEABLE);
		return (SNMP_ERR_NO_CREATION);

	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		abort();
	}
	abort();

  get:
	switch (value->var.subs[sub - 1]) {

	case LEAF_hrPartitionIndex:
		value->v.integer = entry->index[1];
		return (SNMP_ERR_NOERROR);

	case LEAF_hrPartitionLabel:
		return (string_get(value, entry->label, -1));

	case LEAF_hrPartitionID:
		return(string_get(value, entry->id, -1));

	case LEAF_hrPartitionSize:
		value->v.integer = entry->size;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrPartitionFSIndex:
		value->v.integer = entry->fs_Index;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}
