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
 * Host Resources MIB for SNMPd. Implementation for hrStorageTable
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>
#include <sys/mount.h>

#include <vm/vm_param.h>

#include <assert.h>
#include <err.h>
#include <limits.h>
#include <memstat.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h> /* for getpagesize() */
#include <sysexits.h>

#include "hostres_snmp.h"
#include "hostres_oid.h"
#include "hostres_tree.h"

/* maximum length for descritpion string according to MIB */
#define	SE_DESC_MLEN	(255 + 1)

/*
 * This structure is used to hold a SNMP table entry
 * for HOST-RESOURCES-MIB's hrStorageTable
 */
struct storage_entry {
	int32_t		index;
	const struct asn_oid *type;
	u_char		*descr;
	int32_t		allocationUnits;
	int32_t		size;
	int32_t		used;
	uint32_t	allocationFailures;
#define	HR_STORAGE_FOUND 0x001
	uint32_t	flags;	/* to be used internally*/
	TAILQ_ENTRY(storage_entry) link;
};
TAILQ_HEAD(storage_tbl, storage_entry);

/*
 * Next structure is used to keep o list of mappings from a specific name
 * (a_name) to an entry in the hrStorageTblEntry. We are trying to keep the
 * same index for a specific name at least for the duration of one SNMP agent
 * run.
 */
struct storage_map_entry {
	int32_t		hrIndex; /* used for storage_entry::index */

	/* map key, also used for storage_entry::descr */
	u_char		*a_name;

	/*
	 * next may be NULL if the respective storage_entry
	 * is (temporally) gone
	 */
	struct storage_entry *entry;
	STAILQ_ENTRY(storage_map_entry) link;
};
STAILQ_HEAD(storage_map, storage_map_entry);

/* the head of the list with table's entries */
static struct storage_tbl storage_tbl = TAILQ_HEAD_INITIALIZER(storage_tbl);

/*for consistent table indexing*/
static struct storage_map storage_map =
    STAILQ_HEAD_INITIALIZER(storage_map);

/* last (agent) tick when hrStorageTable was updated */
static uint64_t storage_tick;

/* maximum number of ticks between two refreshs */
uint32_t storage_tbl_refresh = HR_STORAGE_TBL_REFRESH * 100;

/* for kvm_getswapinfo, malloc'd */
static struct kvm_swap *swap_devs;
static size_t swap_devs_len;		/* item count for swap_devs */

/* for getfsstat, malloc'd */
static struct statfs *fs_buf;
static size_t fs_buf_count;		/* item count for fs_buf */

static struct vmtotal mem_stats;

/* next int available for indexing the hrStorageTable */
static uint32_t next_storage_index = 1;

/* start of list for memory detailed stats */
static struct memory_type_list *mt_list;

/* Constants */
static const struct asn_oid OIDX_hrStorageRam_c = OIDX_hrStorageRam;
static const struct asn_oid OIDX_hrStorageVirtualMemory_c =
    OIDX_hrStorageVirtualMemory;

/**
 * Create a new entry into the storage table and, if necessary, an
 * entry into the storage map.
 */
static struct storage_entry *
storage_entry_create(const char *name)
{
	struct storage_entry *entry;
	struct storage_map_entry *map;
	size_t name_len;

	assert(name != NULL);
	assert(strlen(name) > 0);

	STAILQ_FOREACH(map, &storage_map, link)
		if (strcmp(map->a_name, name) == 0)
			break;

	if (map == NULL) {
		/* new object - get a new index */
		if (next_storage_index > INT_MAX) {
			syslog(LOG_ERR,
			    "%s: hrStorageTable index wrap", __func__);
			errx(EX_SOFTWARE, "hrStorageTable index wrap");
		}

		if ((map = malloc(sizeof(*map))) == NULL) {
			syslog(LOG_ERR, "hrStorageTable: %s: %m", __func__ );
			return (NULL);
		}

		name_len = strlen(name) + 1;
		if (name_len > SE_DESC_MLEN)
			name_len = SE_DESC_MLEN;

		if ((map->a_name = malloc(name_len)) == NULL) {
			free(map);
			return (NULL);
		}

		strlcpy(map->a_name, name, name_len);
		map->hrIndex = next_storage_index++;

		STAILQ_INSERT_TAIL(&storage_map, map, link);

		HRDBG("%s added into hrStorageMap at index=%d",
		    name, map->hrIndex);
	} else {
		HRDBG("%s exists in hrStorageMap index=%d\n",
		    name, map->hrIndex);
	}

	if ((entry = malloc(sizeof(*entry))) == NULL) {
		syslog(LOG_WARNING, "%s: %m", __func__);
		return (NULL);
	}
	memset(entry, 0, sizeof(*entry));

	entry->index = map->hrIndex;

	if ((entry->descr = strdup(map->a_name)) == NULL) {
		free(entry);
		return (NULL);
	}

	map->entry = entry;

	INSERT_OBJECT_INT(entry, &storage_tbl);

	return (entry);
}

/**
 * Delete an entry from the storage table.
 */
static void
storage_entry_delete(struct storage_entry *entry)
{
	struct storage_map_entry *map;

	assert(entry != NULL);

	TAILQ_REMOVE(&storage_tbl, entry, link);
	STAILQ_FOREACH(map, &storage_map, link)
		if (map->entry == entry) {
			map->entry = NULL;
			break;
		}
	free(entry->descr);
	free(entry);
}

/**
 * Find a table entry by its name.
 */
static struct storage_entry *
storage_find_by_name(const char *name)
{
	struct storage_entry *entry;

	TAILQ_FOREACH(entry, &storage_tbl, link)
		if (strcmp(entry->descr, name) == 0)
			return (entry);

	return (NULL);
}

/*
 * VM info.
 */
static void
storage_OS_get_vm(void)
{
	int mib[2] = { CTL_VM, VM_TOTAL };
	size_t len = sizeof(mem_stats);
	int page_size_bytes;
	struct storage_entry *entry;

	if (sysctl(mib, 2, &mem_stats, &len, NULL, 0) < 0) {
		syslog(LOG_ERR,
		    "hrStoragetable: %s: sysctl({CTL_VM, VM_METER}) "
		    "failed: %m", __func__);
		assert(0);
		return;
	}

	page_size_bytes = getpagesize();

	/* Real Memory Metrics */
	if ((entry = storage_find_by_name("Real Memory Metrics")) == NULL &&
	    (entry = storage_entry_create("Real Memory Metrics")) == NULL)
		return; /* I'm out of luck now, maybe next time */

	entry->flags |= HR_STORAGE_FOUND;
	entry->type = &OIDX_hrStorageRam_c;
	entry->allocationUnits = page_size_bytes;
	entry->size = mem_stats.t_rm;
	entry->used = mem_stats.t_arm; /* ACTIVE is not USED - FIXME */
	entry->allocationFailures = 0;

	/* Shared Real Memory Metrics */
	if ((entry = storage_find_by_name("Shared Real Memory Metrics")) ==
	    NULL &&
	    (entry = storage_entry_create("Shared Real Memory Metrics")) ==
	    NULL)
		return;

	entry->flags |= HR_STORAGE_FOUND;
	entry->type = &OIDX_hrStorageRam_c;
	entry->allocationUnits = page_size_bytes;
	entry->size = mem_stats.t_rmshr;
	/* ACTIVE is not USED - FIXME */
	entry->used = mem_stats.t_armshr;
	entry->allocationFailures = 0;
}

static void
storage_OS_get_memstat(void)
{
	struct memory_type *mt_item;
	struct storage_entry *entry;

	if (mt_list == NULL) {
		if ((mt_list = memstat_mtl_alloc()) == NULL)
			/* again? we have a serious problem */
		return;
	}

	if (memstat_sysctl_all(mt_list, 0) < 0) {
		syslog(LOG_ERR, "memstat_sysctl_all failed: %s",
		    memstat_strerror(memstat_mtl_geterror(mt_list)) );
		return;
	}

	if ((mt_item = memstat_mtl_first(mt_list)) == NULL) {
		/* usually this is not an error, no errno for this failure*/
		HRDBG("memstat_mtl_first failed");
		return;
	}

	do {
		const char *memstat_name;
		uint64_t tmp_size;
		int allocator;
		char alloc_descr[SE_DESC_MLEN];

		memstat_name = memstat_get_name(mt_item);

		if (memstat_name == NULL || strlen(memstat_name) == 0)
			continue;

		switch (allocator = memstat_get_allocator(mt_item)) {

		  case ALLOCATOR_MALLOC:
			snprintf(alloc_descr, sizeof(alloc_descr),
			    "MALLOC: %s", memstat_name);
			break;

		  case ALLOCATOR_UMA:
			snprintf(alloc_descr, sizeof(alloc_descr),
			    "UMA: %s", memstat_name);
			break;

		  default:
			snprintf(alloc_descr, sizeof(alloc_descr),
			    "UNKNOWN%d: %s", allocator, memstat_name);
			break;
		}

		if ((entry = storage_find_by_name(alloc_descr)) == NULL &&
		    (entry = storage_entry_create(alloc_descr)) == NULL)
			return;

		entry->flags |= HR_STORAGE_FOUND;
		entry->type = &OIDX_hrStorageRam_c;

		if ((tmp_size = memstat_get_size(mt_item)) == 0)
			tmp_size = memstat_get_sizemask(mt_item);
		entry->allocationUnits =
		    (tmp_size  > INT_MAX ? INT_MAX : (int32_t)tmp_size);

		tmp_size  = memstat_get_countlimit(mt_item);
		entry->size =
		    (tmp_size  > INT_MAX ? INT_MAX : (int32_t)tmp_size);

		tmp_size = memstat_get_count(mt_item);
		entry->used =
		    (tmp_size  > INT_MAX ? INT_MAX : (int32_t)tmp_size);

		tmp_size = memstat_get_failures(mt_item);
		entry->allocationFailures =
		    (tmp_size  > INT_MAX ? INT_MAX : (int32_t)tmp_size);

	} while((mt_item = memstat_mtl_next(mt_item)) != NULL);
}

/**
 * Get swap info
 */
static void
storage_OS_get_swap(void)
{
	struct storage_entry *entry;
	char swap_w_prefix[SE_DESC_MLEN];
	size_t len;
	int nswapdev;

	len = sizeof(nswapdev);
	nswapdev = 0;

	if (sysctlbyname("vm.nswapdev", &nswapdev, &len, NULL,0 ) < 0) {
		syslog(LOG_ERR,
		    "hrStorageTable: sysctlbyname(\"vm.nswapdev\") "
		    "failed. %m");
		assert(0);
		return;
	}

	if (nswapdev <= 0) {
		HRDBG("vm.nswapdev is %d", nswapdev);
		return;
	}

	if (nswapdev + 1 != (int)swap_devs_len || swap_devs == NULL) {
		swap_devs_len = nswapdev + 1;
		swap_devs = reallocf(swap_devs,
		    swap_devs_len * sizeof(struct kvm_swap));

		assert(swap_devs != NULL);
		if (swap_devs == NULL) {
			swap_devs_len = 0;
			return;
		}
	}

	nswapdev = kvm_getswapinfo(hr_kd, swap_devs, swap_devs_len, 0);
	if (nswapdev < 0) {
		syslog(LOG_ERR,
		    "hrStorageTable: kvm_getswapinfo failed. %m\n");
		assert(0);
		return;
	}

	for (len = 0; len < (size_t)nswapdev; len++) {
		memset(&swap_w_prefix[0], '\0', sizeof(swap_w_prefix));
		snprintf(swap_w_prefix, sizeof(swap_w_prefix) - 1,
		    "Swap:%s%s", _PATH_DEV, swap_devs[len].ksw_devname);

		entry = storage_find_by_name(swap_w_prefix);
		if (entry == NULL)
			entry = storage_entry_create(swap_w_prefix);

		assert (entry != NULL);
		if (entry == NULL)
			return; /* Out of luck */

		entry->flags |= HR_STORAGE_FOUND;
		entry->type = &OIDX_hrStorageVirtualMemory_c;
		entry->allocationUnits = getpagesize();
		entry->size = swap_devs[len].ksw_total;
		entry->used = swap_devs[len].ksw_used;
		entry->allocationFailures = 0;
	}
}

/**
 * Query the underlaying OS for the mounted file systems
 * anf fill in the respective lists (for hrStorageTable and for hrFSTable)
 */
static void
storage_OS_get_fs(void)
{
	struct storage_entry *entry;
	uint64_t size, used;
	int i, mounted_fs_count, units;
	char fs_string[SE_DESC_MLEN];

	if ((mounted_fs_count = getfsstat(NULL, 0, MNT_NOWAIT)) < 0) {
		syslog(LOG_ERR, "hrStorageTable: getfsstat() failed: %m");
		return; /* out of luck this time */
	}

	if (mounted_fs_count != (int)fs_buf_count || fs_buf == NULL) {
		fs_buf_count = mounted_fs_count;
		fs_buf = reallocf(fs_buf, fs_buf_count * sizeof(struct statfs));
		if (fs_buf == NULL) {
			fs_buf_count = 0;
			assert(0);
			return;
		}
	}

	if ((mounted_fs_count = getfsstat(fs_buf,
	    fs_buf_count * sizeof(struct statfs), MNT_NOWAIT)) < 0) {
		syslog(LOG_ERR, "hrStorageTable: getfsstat() failed: %m");
		return; /* out of luck this time */
	}

	HRDBG("got %d mounted FS", mounted_fs_count);

	fs_tbl_pre_refresh();

	for (i = 0; i < mounted_fs_count; i++) {
		snprintf(fs_string, sizeof(fs_string),
		    "%s, type: %s, dev: %s", fs_buf[i].f_mntonname,
		    fs_buf[i].f_fstypename, fs_buf[i].f_mntfromname);

		entry = storage_find_by_name(fs_string);
		if (entry == NULL)
			entry = storage_entry_create(fs_string);

		assert (entry != NULL);
		if (entry == NULL)
			return; /* Out of luck */

		entry->flags |= HR_STORAGE_FOUND;
		entry->type = fs_get_type(&fs_buf[i]); /*XXX - This is wrong*/

		units = fs_buf[i].f_bsize;
		size = fs_buf[i].f_blocks;
		used = fs_buf[i].f_blocks - fs_buf[i].f_bfree;
		while (size > INT_MAX) {
			units <<= 1;
			size >>= 1;
			used >>= 1;
		}
		entry->allocationUnits = units;
		entry->size = size;
		entry->used = used;

		entry->allocationFailures = 0;

		/* take care of hrFSTable */
		fs_tbl_process_statfs_entry(&fs_buf[i], entry->index);
	}

	fs_tbl_post_refresh();
}

/**
 * Initialize storage table and populate it.
 */
void
init_storage_tbl(void)
{
	if ((mt_list = memstat_mtl_alloc()) == NULL)
		syslog(LOG_ERR,
		    "hrStorageTable: memstat_mtl_alloc() failed: %m");

	refresh_storage_tbl(1);
}

void
fini_storage_tbl(void)
{
	struct storage_map_entry *n1;

	if (swap_devs != NULL) {
		free(swap_devs);
		swap_devs = NULL;
	}
	swap_devs_len = 0;

	if (fs_buf != NULL) {
		free(fs_buf);
		fs_buf = NULL;
	}
	fs_buf_count = 0;

	while ((n1 = STAILQ_FIRST(&storage_map)) != NULL) {
		STAILQ_REMOVE_HEAD(&storage_map, link);
		if (n1->entry != NULL) {
			TAILQ_REMOVE(&storage_tbl, n1->entry, link);
			free(n1->entry->descr);
			free(n1->entry);
		}
		free(n1->a_name);
		free(n1);
	}
	assert(TAILQ_EMPTY(&storage_tbl));
}

void
refresh_storage_tbl(int force)
{
	struct storage_entry *entry, *entry_tmp;

	if (!force && storage_tick != 0 &&
	    this_tick - storage_tick < storage_tbl_refresh) {
		HRDBG("no refresh needed");
		return;
	}

	/* mark each entry as missing */
	TAILQ_FOREACH(entry, &storage_tbl, link)
		entry->flags &= ~HR_STORAGE_FOUND;

	storage_OS_get_vm();
	storage_OS_get_swap();
	storage_OS_get_fs();
	storage_OS_get_memstat();

	/*
	 * Purge items that disappeared
	 */
	TAILQ_FOREACH_SAFE(entry, &storage_tbl, link, entry_tmp)
		if (!(entry->flags & HR_STORAGE_FOUND))
			storage_entry_delete(entry);

	storage_tick = this_tick;

	HRDBG("refresh DONE");
}

/*
 * This is the implementation for a generated (by our SNMP tool)
 * function prototype, see hostres_tree.h
 * It handles the SNMP operations for hrStorageTable
 */
int
op_hrStorageTable(struct snmp_context *ctx __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op curr_op)
{
	struct storage_entry *entry;

	refresh_storage_tbl(0);

	switch (curr_op) {

	case SNMP_OP_GETNEXT:
		if ((entry = NEXT_OBJECT_INT(&storage_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);

		value->var.len = sub + 1;
		value->var.subs[sub] = entry->index;
		goto get;

	case SNMP_OP_GET:
		if ((entry = FIND_OBJECT_INT(&storage_tbl,
		    &value->var, sub)) == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		goto get;

	case SNMP_OP_SET:
		if ((entry = FIND_OBJECT_INT(&storage_tbl,
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

	case LEAF_hrStorageIndex:
		value->v.integer = entry->index;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrStorageType:
		assert(entry->type != NULL);
		value->v.oid = *entry->type;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrStorageDescr:
		assert(entry->descr != NULL);
		return (string_get(value, entry->descr, -1));
		break;

	case LEAF_hrStorageAllocationUnits:
		value->v.integer = entry->allocationUnits;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrStorageSize:
		value->v.integer = entry->size;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrStorageUsed:
		value->v.integer = entry->used;
		return (SNMP_ERR_NOERROR);

	case LEAF_hrStorageAllocationFailures:
		value->v.uint32 = entry->allocationFailures;
		return (SNMP_ERR_NOERROR);
	}
	abort();
}
