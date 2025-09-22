/*
 * dbcreate.c -- routines to create an nsd(8) name database
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "namedb.h"
#include "udb.h"
#include "options.h"
#include "nsd.h"
#include "ixfr.h"

/* pathname directory separator character */
#define PATHSEP '/'

/** add an rdata (uncompressed) to the destination */
static size_t
add_rdata(rr_type* rr, unsigned i, uint8_t* buf, size_t buflen)
{
	switch(rdata_atom_wireformat_type(rr->type, i)) {
		case RDATA_WF_COMPRESSED_DNAME:
		case RDATA_WF_UNCOMPRESSED_DNAME:
		{
			const dname_type* dname = domain_dname(
				rdata_atom_domain(rr->rdatas[i]));
			if(dname->name_size > buflen)
				return 0;
			memmove(buf, dname_name(dname), dname->name_size);
			return dname->name_size;
		}
		default:
			break;
	}
	if(rdata_atom_size(rr->rdatas[i]) > buflen)
		return 0;
	memmove(buf, rdata_atom_data(rr->rdatas[i]),
		rdata_atom_size(rr->rdatas[i]));
	return rdata_atom_size(rr->rdatas[i]);
}

/* marshal rdata into buffer, must be MAX_RDLENGTH in size */
size_t
rr_marshal_rdata(rr_type* rr, uint8_t* rdata, size_t sz)
{
	size_t len = 0;
	unsigned i;
	assert(rr);
	for(i=0; i<rr->rdata_count; i++) {
		len += add_rdata(rr, i, rdata+len, sz-len);
	}
	return len;
}

int
print_rrs(FILE* out, struct zone* zone)
{
	rrset_type *rrset;
	domain_type *domain = zone->apex;
	region_type* region = region_create(xalloc, free);
	region_type* rr_region = region_create(xalloc, free);
	buffer_type* rr_buffer = buffer_create(region, MAX_RDLENGTH);
	struct state_pretty_rr* state = create_pretty_rr(region);
	/* first print the SOA record for the zone */
	if(zone->soa_rrset) {
		size_t i;
		for(i=0; i < zone->soa_rrset->rr_count; i++) {
			if(!print_rr(out, state, &zone->soa_rrset->rrs[i],
				rr_region, rr_buffer)){
				log_msg(LOG_ERR, "There was an error "
				   "printing SOARR to zone %s",
				   zone->opts->name);
				region_destroy(region);
				region_destroy(rr_region);
				return 0;
			}
		}
	}
	/* go through entire tree below the zone apex (incl subzones) */
	while(domain && domain_is_subdomain(domain, zone->apex))
	{
		for(rrset = domain->rrsets; rrset; rrset=rrset->next)
		{
			size_t i;
			if(rrset->zone != zone || rrset == zone->soa_rrset)
				continue;
			for(i=0; i < rrset->rr_count; i++) {
				if(!print_rr(out, state, &rrset->rrs[i],
					rr_region, rr_buffer)){
					log_msg(LOG_ERR, "There was an error "
					   "printing RR to zone %s",
					   zone->opts->name);
					region_destroy(region);
					region_destroy(rr_region);
					return 0;
				}
			}
		}
		domain = domain_next(domain);
	}
	region_destroy(region);
	region_destroy(rr_region);
	return 1;
}

static int
print_header(zone_type* zone, FILE* out, time_t* now, const char* logs)
{
	char buf[4096+16];
	/* ctime prints newline at end of this line */
	snprintf(buf, sizeof(buf), "; zone %s written by NSD %s on %s",
		zone->opts->name, PACKAGE_VERSION, ctime(now));
	if(!write_data(out, buf, strlen(buf)))
		return 0;
	if(!logs || logs[0] == 0) return 1;
	snprintf(buf, sizeof(buf), "; %s\n", logs);
	return write_data(out, buf, strlen(buf));
}

static int
write_to_zonefile(zone_type* zone, const char* filename, const char* logs)
{
	time_t now = time(0);
	FILE *out = fopen(filename, "w");
	if(!out) {
		log_msg(LOG_ERR, "cannot write zone %s file %s: %s",
			zone->opts->name, filename, strerror(errno));
		return 0;
	}
	if(!print_header(zone, out, &now, logs)) {
		fclose(out);
		log_msg(LOG_ERR, "There was an error printing "
			"the header to zone %s", zone->opts->name);
		return 0;
	}
	if(!print_rrs(out, zone)) {
		fclose(out);
		return 0;
	}
	if(fclose(out) != 0) {
		log_msg(LOG_ERR, "cannot write zone %s to file %s: fclose: %s",
			zone->opts->name, filename, strerror(errno));
		return 0;
	}
	return 1;
}

/** create directories above this file, .../dir/dir/dir/file */
int
create_dirs(const char* path)
{
	char dir[4096];
	char* p;
	strlcpy(dir, path, sizeof(dir));
	/* if we start with / then do not try to create '' */
	if(dir[0] == PATHSEP)
		p = strchr(dir+1, PATHSEP);
	else	p = strchr(dir, PATHSEP);
	/* create each directory component from the left */
	while(p) {
		assert(*p == PATHSEP);
		*p = 0; /* end the directory name here */
		if(mkdir(dir
#ifndef MKDIR_HAS_ONE_ARG
			, 0750
#endif
			) == -1) {
			if(errno != EEXIST) {
				log_msg(LOG_ERR, "create dir %s: %s",
					dir, strerror(errno));
				*p = PATHSEP; /* restore input string */
				return 0;
			}
			/* it already exists, OK, continue */
		}
		*p = PATHSEP;
		p = strchr(p+1, PATHSEP);
	}
	return 1;
}

/** create pathname components and check if file exists */
static int
create_path_components(const char* path, int* notexist)
{
	/* stat the file, to see if it exists, and if its directories exist */
	struct stat s;
	if(stat(path, &s) != 0) {
		if(errno == ENOENT) {
			*notexist = 1;
			/* see if we need to create pathname components */
			return create_dirs(path);
		}
		log_msg(LOG_ERR, "cannot stat %s: %s", path, strerror(errno));
		return 0;
	}
	*notexist = 0;
	return 1;
}

void
namedb_write_zonefile(struct nsd* nsd, struct zone_options* zopt)
{
	const char* zfile;
	int notexist = 0;
	zone_type* zone;
	/* if no zone exists, it has no contents or it has no zonefile
	 * configured, then no need to write data to disk */
	if(!zopt->pattern->zonefile)
		return;
	zone = namedb_find_zone(nsd->db, (const dname_type*)zopt->node.key);
	if(!zone || !zone->apex || !zone->soa_rrset)
		return;
	/* write if file does not exist, or if changed */
	/* so, determine filename, create directory components, check exist*/
	zfile = config_make_zonefile(zopt, nsd);
	if(!create_path_components(zfile, &notexist)) {
		log_msg(LOG_ERR, "could not write zone %s to file %s because "
			"the path could not be created", zopt->name, zfile);
		return;
	}

	/* if not changed, do not write. */
	if(notexist || zone->is_changed) {
		char logs[4096];
		char bakfile[4096];
		struct timespec mtime;
		/* write to zfile~ first, then rename if that works */
		snprintf(bakfile, sizeof(bakfile), "%s~", zfile);
		if(zone->logstr)
			strlcpy(logs, zone->logstr, sizeof(logs));
		else
			logs[0] = 0;
		VERBOSITY(1, (LOG_INFO, "writing zone %s to file %s",
			zone->opts->name, zfile));
		if(!write_to_zonefile(zone, bakfile, logs)) {
			(void)unlink(bakfile); /* delete failed file */
			return; /* error already printed */
		}
		if(rename(bakfile, zfile) == -1) {
			log_msg(LOG_ERR, "rename(%s to %s) failed: %s",
				bakfile, zfile, strerror(errno));
			(void)unlink(bakfile); /* delete failed file */
			return;
		}
		zone->is_changed = 0;
		VERBOSITY(3, (LOG_INFO, "zone %s written to file %s",
			zone->opts->name, zfile));
		/* fetch the mtime of the just created zonefile so we
		 * do not waste effort reading it back in */
		if(!file_get_mtime(zfile, &mtime, &notexist)) {
			get_time(&mtime);
		}
		zone->mtime = mtime;
		if(zone->filename)
			region_recycle(nsd->db->region, zone->filename,
				strlen(zone->filename)+1);
		zone->filename = region_strdup(nsd->db->region, zfile);
		if(zone->logstr)
			region_recycle(nsd->db->region, zone->logstr,
				strlen(zone->logstr)+1);
		zone->logstr = NULL;
		if(zone_is_ixfr_enabled(zone) && zone->ixfr)
			ixfr_write_to_file(zone, zfile);
	}
}

void
namedb_write_zonefiles(struct nsd* nsd, struct nsd_options* options)
{
	struct zone_options* zo;
	RBTREE_FOR(zo, struct zone_options*, options->zone_options) {
		namedb_write_zonefile(nsd, zo);
	}
}
