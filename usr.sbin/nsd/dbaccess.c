/*
 * dbaccess.c -- access methods for nsd(8) database
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "dns.h"
#include "namedb.h"
#include "util.h"
#include "options.h"
#include "rdata.h"
#include "udb.h"
#include "zonec.h"
#include "nsec3.h"
#include "difffile.h"
#include "nsd.h"
#include "ixfr.h"
#include "ixfrcreate.h"

void
namedb_close(struct namedb* db)
{
	if(db) {
		region_destroy(db->region);
	}
}

void
namedb_free_ixfr(struct namedb* db)
{
	struct radnode* n;
	for(n=radix_first(db->zonetree); n; n=radix_next(n)) {
		zone_ixfr_free(((zone_type*)n->elem)->ixfr);
	}
}

/** create a zone */
zone_type*
namedb_zone_create(namedb_type* db, const dname_type* dname,
	struct zone_options* zo)
{
	zone_type* zone = (zone_type *) region_alloc(db->region,
		sizeof(zone_type));
	zone->node = radname_insert(db->zonetree, dname_name(dname),
		dname->name_size, zone);
	assert(zone->node);
	zone->apex = domain_table_insert(db->domains, dname);
	zone->apex->usage++; /* the zone.apex reference */
	zone->apex->is_apex = 1;
	zone->soa_rrset = NULL;
	zone->soa_nx_rrset = NULL;
	zone->ns_rrset = NULL;
#ifdef NSEC3
	zone->nsec3_param = NULL;
	zone->nsec3_last = NULL;
	zone->nsec3tree = NULL;
	zone->hashtree = NULL;
	zone->wchashtree = NULL;
	zone->dshashtree = NULL;
#endif
	zone->opts = zo;
	zone->ixfr = NULL;
	zone->filename = NULL;
	zone->includes.count = 0;
	zone->includes.paths = NULL;
	zone->logstr = NULL;
	zone->mtime.tv_sec = 0;
	zone->mtime.tv_nsec = 0;
	zone->zonestatid = 0;
	zone->is_secure = 0;
	zone->is_changed = 0;
	zone->is_updated = 0;
	zone->is_skipped = 0;
	zone->is_checked = 0;
	zone->is_bad = 0;
	zone->is_ok = 1;
	return zone;
}

void
namedb_zone_free_filenames(namedb_type *db, zone_type* zone)
{
	assert(!zone->includes.paths == !zone->includes.count);

	if (zone->filename) {
		region_recycle(
			db->region, zone->filename, strlen(zone->filename) + 1);
		zone->filename = NULL;
	}

	if (zone->includes.count) {
		for (size_t i=0; i < zone->includes.count; i++) {
			region_recycle(
				db->region,
				zone->includes.paths[i],
				strlen(zone->includes.paths[i]) + 1);
		}

		region_recycle(
			db->region,
			zone->includes.paths,
			zone->includes.count * sizeof(*zone->includes.paths));
		zone->includes.count = 0;
		zone->includes.paths = NULL;
	}
}

void
namedb_zone_delete(namedb_type* db, zone_type* zone)
{
	/* RRs and UDB and NSEC3 and so on must be already deleted */
	radix_delete(db->zonetree, zone->node);

	/* see if apex can be deleted */
	if(zone->apex) {
		zone->apex->usage --;
		zone->apex->is_apex = 0;
		if(zone->apex->usage == 0) {
			/* delete the apex, possibly */
			domain_table_deldomain(db, zone->apex);
		}
	}

	/* soa_rrset is freed when the SOA was deleted */
	if(zone->soa_nx_rrset) {
		region_recycle(db->region, zone->soa_nx_rrset->rrs,
			sizeof(rr_type));
		region_recycle(db->region, zone->soa_nx_rrset,
			sizeof(rrset_type));
	}
#ifdef NSEC3
	hash_tree_delete(db->region, zone->nsec3tree);
	hash_tree_delete(db->region, zone->hashtree);
	hash_tree_delete(db->region, zone->wchashtree);
	hash_tree_delete(db->region, zone->dshashtree);
#endif
	zone_ixfr_free(zone->ixfr);
	namedb_zone_free_filenames(db, zone);
	if(zone->logstr)
		region_recycle(db->region, zone->logstr,
			strlen(zone->logstr)+1);
	region_recycle(db->region, zone, sizeof(zone_type));
}

struct namedb *
namedb_open (struct nsd_options* opt)
{
	namedb_type* db;

	/*
	 * Region used to store the loaded database.  The region is
	 * freed in namedb_close.
	 */
	region_type* db_region;

	(void)opt;

#ifdef USE_MMAP_ALLOC
	db_region = region_create_custom(mmap_alloc, mmap_free, MMAP_ALLOC_CHUNK_SIZE,
		MMAP_ALLOC_LARGE_OBJECT_SIZE, MMAP_ALLOC_INITIAL_CLEANUP_SIZE, 1);
#else /* !USE_MMAP_ALLOC */
	db_region = region_create_custom(xalloc, free, DEFAULT_CHUNK_SIZE,
		DEFAULT_LARGE_OBJECT_SIZE, DEFAULT_INITIAL_CLEANUP_SIZE, 1);
#endif /* !USE_MMAP_ALLOC */
	db = (namedb_type *) region_alloc(db_region, sizeof(struct namedb));
	db->region = db_region;
	db->domains = domain_table_create(db->region);
	db->zonetree = radix_tree_create(db->region);
	db->diff_skip = 0;
	db->diff_pos = 0;

	if (gettimeofday(&(db->diff_timestamp), NULL) != 0) {
		log_msg(LOG_ERR, "unable to load namedb: cannot initialize timestamp");
		region_destroy(db_region);
		return NULL;
	}

	return db;
}

/** get the file mtime stat (or nonexist or error) */
int
file_get_mtime(const char* file, struct timespec* mtime, int* nonexist)
{
	struct stat s;
	if(stat(file, &s) != 0) {
		mtime->tv_sec = 0;
		mtime->tv_nsec = 0;
		*nonexist = (errno == ENOENT);
		return 0;
	}
	*nonexist = 0;
	mtime->tv_sec = s.st_mtime;
#ifdef HAVE_STRUCT_STAT_ST_MTIMENSEC
	mtime->tv_nsec = s.st_mtimensec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	mtime->tv_nsec = s.st_mtim.tv_nsec;
#else
	mtime->tv_nsec = 0;
#endif
	return 1;
}

void
namedb_read_zonefile(struct nsd* nsd, struct zone* zone, udb_base* taskudb,
	udb_ptr* last_task)
{
	struct timespec mtime;
	int nonexist = 0;
	unsigned int errors;
	const char* fname;
	struct ixfr_create* ixfrcr = NULL;
	int ixfr_create_already_done = 0;
	if(!nsd->db || !zone || !zone->opts || !zone->opts->pattern->zonefile)
		return;
	mtime.tv_sec = 0;
	mtime.tv_nsec = 0;
	fname = config_make_zonefile(zone->opts, nsd);
	assert(fname);
	if(!file_get_mtime(fname, &mtime, &nonexist)) {
		if(nonexist) {
			if(zone_is_slave(zone->opts)) {
				/* for slave zones not as bad, no zonefile
				 * may just mean we have to transfer it */
				VERBOSITY(2, (LOG_INFO, "zonefile %s does not exist",
					fname));
			} else {
				/* without a download option, we can never
				 * serve data, more severe error printout */
				log_msg(LOG_ERR, "zonefile %s does not exist", fname);
			}

		} else
			log_msg(LOG_ERR, "zonefile %s: %s",
				fname, strerror(errno));
		if(taskudb) task_new_soainfo(taskudb, last_task, zone, 0);
		return;
	} else {
		const char* zone_fname = zone->filename;
		struct timespec zone_mtime = zone->mtime;
		/* if no zone_fname, then it was acquired in zone transfer,
		 * see if the file is newer than the zone transfer
		 * (regardless if this is a different file), because the
		 * zone transfer is a different content source too */
		if(!zone_fname && timespec_compare(&zone_mtime, &mtime) >= 0) {
			VERBOSITY(3, (LOG_INFO, "zonefile %s is older than "
				"zone transfer in memory", fname));
			return;

		/* if zone_fname, then the file was acquired from reading it,
		 * and see if filename changed or mtime newer to read it */
		} else if(zone_fname && strcmp(zone_fname, fname) == 0 &&
			timespec_compare(&zone_mtime, &mtime) == 0) {
			int changed = 0;
			struct timespec include_mtime;
			/* one of the includes may have been deleted, changed, etc */
			for (size_t i=0; i < zone->includes.count; i++) {
				if (!file_get_mtime(zone->includes.paths[i], &include_mtime, &nonexist)) {
					changed = 1;
				} else if (timespec_compare(&zone_mtime, &include_mtime) < 0) {
					mtime = include_mtime;
					changed = 1;
				}
			}

			if (!changed) {
				VERBOSITY(3, (LOG_INFO, "zonefile %s is not modified",
					fname));
				return;
			}
		}
	}

	if(ixfr_create_from_difference(zone, fname,
		&ixfr_create_already_done)) {
		ixfrcr = ixfr_create_start(zone, fname,
			zone->opts->pattern->ixfr_size, 0);
		if(!ixfrcr) {
			/* leaves the ixfrcr at NULL, so it is not created */
			log_msg(LOG_ERR, "out of memory starting ixfr create");
		}
	}

	namedb_zone_free_filenames(nsd->db, zone);
	zone->filename = region_strdup(nsd->db->region, fname);

	/* wipe zone from memory */
#ifdef NSEC3
	nsec3_clear_precompile(nsd->db, zone);
	zone->nsec3_param = NULL;
#endif
	delete_zone_rrs(nsd->db, zone);
	VERBOSITY(5, (LOG_INFO, "zone %s zonec_read(%s)",
		zone->opts->name, fname));
	errors = zonec_read(nsd->db, nsd->db->domains, zone->opts->name, fname, zone);
	if(errors > 0) {
		log_msg(LOG_ERR, "zone %s file %s read with %u errors",
			zone->opts->name, fname, errors);
		/* wipe (partial) zone from memory */
		zone->is_ok = 1;
#ifdef NSEC3
		nsec3_clear_precompile(nsd->db, zone);
		zone->nsec3_param = NULL;
#endif
		delete_zone_rrs(nsd->db, zone);
		namedb_zone_free_filenames(nsd->db, zone);
		if(zone->logstr)
			region_recycle(nsd->db->region, zone->logstr,
				strlen(zone->logstr)+1);
		zone->logstr = NULL;
	} else {
		VERBOSITY(1, (LOG_INFO, "zone %s read with success",
			zone->opts->name));
		zone->is_ok = 1;
		zone->is_changed = 0;
		/* store zone into udb */
		zone->mtime = mtime;
		if(zone->logstr)
			region_recycle(nsd->db->region, zone->logstr,
				strlen(zone->logstr)+1);
		zone->logstr = NULL;
		if(ixfr_create_already_done) {
			ixfr_readup_exist(zone, nsd, fname);
		} else if(ixfrcr) {
			if(!ixfr_create_perform(ixfrcr, zone, 1, nsd, fname,
				zone->opts->pattern->ixfr_number)) {
				log_msg(LOG_ERR, "failed to create IXFR");
			} else {
				VERBOSITY(2, (LOG_INFO, "zone %s created IXFR %s.ixfr",
					zone->opts->name, fname));
			}
			ixfr_create_free(ixfrcr);
		} else if(zone_is_ixfr_enabled(zone)) {
			ixfr_read_from_file(nsd, zone, fname);
		}
	}
	if(taskudb) task_new_soainfo(taskudb, last_task, zone, 0);
#ifdef NSEC3
	prehash_zone_complete(nsd->db, zone);
#endif
}

void namedb_check_zonefile(struct nsd* nsd, udb_base* taskudb,
	udb_ptr* last_task, struct zone_options* zopt)
{
	zone_type* zone;
	const dname_type* dname = (const dname_type*)zopt->node.key;
	/* find zone to go with it, or create it */
	zone = namedb_find_zone(nsd->db, dname);
	if(!zone) {
		zone = namedb_zone_create(nsd->db, dname, zopt);
	}
	namedb_read_zonefile(nsd, zone, taskudb, last_task);
}

void namedb_check_zonefiles(struct nsd* nsd, struct nsd_options* opt,
	udb_base* taskudb, udb_ptr* last_task)
{
	struct zone_options* zo;
	/* check all zones in opt, create if not exist in main db */
	RBTREE_FOR(zo, struct zone_options*, opt->zone_options) {
		namedb_check_zonefile(nsd, taskudb, last_task, zo);
		if(nsd->signal_hint_shutdown) break;
	}
}
