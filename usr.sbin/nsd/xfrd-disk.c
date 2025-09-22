/*
 * xfrd-disk.c - XFR (transfer) Daemon TCP system source file. Read/Write state to disk.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "xfrd-disk.h"
#include "xfrd.h"
#include "buffer.h"
#include "nsd.h"
#include "options.h"

/* quick tokenizer, reads words separated by whitespace.
   No quoted strings. Comments are skipped (#... eol). */
static char*
xfrd_read_token(FILE* in)
{
	static char buf[4000];
	buf[sizeof(buf)-1]=0;
	while(1) {
		if(fscanf(in, " %3990s", buf) != 1)
			return 0;

		if(buf[0] != '#')
			return buf;

		if(!fgets(buf, sizeof(buf), in))
			return 0;
	}
}

static int
xfrd_read_i16(FILE *in, uint16_t* v)
{
	char* p = xfrd_read_token(in);
	if(!p)
		return 0;

	*v=atoi(p);
	return 1;
}

static int
xfrd_read_i32(FILE *in, uint32_t* v)
{
	char* p = xfrd_read_token(in);
	if(!p)
		return 0;

	*v=atoi(p);
	return 1;
}

static int
xfrd_read_time_t(FILE *in, time_t* v)
{
	char* p = xfrd_read_token(in);
	if(!p)
		return 0;

	*v=atol(p);
	return 1;
}

static int
xfrd_read_check_str(FILE* in, const char* str)
{
	char *p = xfrd_read_token(in);
	if(!p)
		return 0;

	if(strcmp(p, str) != 0)
		return 0;

	return 1;
}

static int
xfrd_read_state_soa(FILE* in, const char* id_acquired,
	const char* id, xfrd_soa_type* soa, time_t* soatime)
{
	char *p;

	if(!xfrd_read_check_str(in, id_acquired) ||
	   !xfrd_read_time_t(in, soatime)) {
		return 0;
	}

	if(*soatime == 0)
		return 1;

	if(!xfrd_read_check_str(in, id) ||
	   !xfrd_read_i16(in, &soa->type) ||
	   !xfrd_read_i16(in, &soa->klass) ||
	   !xfrd_read_i32(in, &soa->ttl) ||
	   !xfrd_read_i16(in, &soa->rdata_count))
	{
		return 0;
	}

	soa->type = htons(soa->type);
	soa->klass = htons(soa->klass);
	soa->ttl = htonl(soa->ttl);
	soa->rdata_count = htons(soa->rdata_count);

	if(!(p=xfrd_read_token(in)) ||
	   !(soa->prim_ns[0] = dname_parse_wire(soa->prim_ns+1, p)))
		return 0;

	if(!(p=xfrd_read_token(in)) ||
	   !(soa->email[0] = dname_parse_wire(soa->email+1, p)))
		return 0;

	if(!xfrd_read_i32(in, &soa->serial) ||
	   !xfrd_read_i32(in, &soa->refresh) ||
	   !xfrd_read_i32(in, &soa->retry) ||
	   !xfrd_read_i32(in, &soa->expire) ||
	   !xfrd_read_i32(in, &soa->minimum))
	{
		return 0;
	}

	soa->serial = htonl(soa->serial);
	soa->refresh = htonl(soa->refresh);
	soa->retry = htonl(soa->retry);
	soa->expire = htonl(soa->expire);
	soa->minimum = htonl(soa->minimum);
	return 1;
}

void
xfrd_read_state(struct xfrd_state* xfrd)
{
	const char* statefile = xfrd->nsd->options->xfrdfile;
	FILE *in;
	uint32_t filetime = 0;
	uint32_t numzones, i;
	region_type *tempregion;
	time_t soa_refresh;

	tempregion = region_create(xalloc, free);
	if(!tempregion)
		return;

	in = fopen(statefile, "r");
	if(!in) {
		if(errno != ENOENT) {
			log_msg(LOG_ERR, "xfrd: Could not open file %s for reading: %s",
				statefile, strerror(errno));
		} else {
			DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: no file %s. refreshing all zones.",
				statefile));
		}
		region_destroy(tempregion);
		return;
	}
	if(!xfrd_read_check_str(in, XFRD_FILE_MAGIC)) {
		/* older file version; reset everything */
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: file %s is old version. refreshing all zones.",
			statefile));
		fclose(in);
		region_destroy(tempregion);
		return;
	}
	if(!xfrd_read_check_str(in, "filetime:") ||
	   !xfrd_read_i32(in, &filetime) ||
	   (time_t)filetime > xfrd_time()+15 ||
	   !xfrd_read_check_str(in, "numzones:") ||
	   !xfrd_read_i32(in, &numzones))
	{
		log_msg(LOG_ERR, "xfrd: corrupt state file %s dated %d (now=%lld)",
			statefile, (int)filetime, (long long)xfrd_time());
		fclose(in);
		region_destroy(tempregion);
		return;
	}

	for(i=0; i<numzones; i++) {
		char *p;
		xfrd_zone_type* zone;
		const dname_type* dname;
		uint32_t state, masnum, nextmas, round_num, timeout, backoff;
		xfrd_soa_type soa_nsd_read, soa_disk_read, soa_notified_read;
		time_t soa_nsd_acquired_read,
			soa_disk_acquired_read, soa_notified_acquired_read;
		xfrd_soa_type incoming_soa;
		time_t incoming_acquired;

		if(nsd.signal_hint_shutdown) {
			fclose(in);
			region_destroy(tempregion);
			return;
		}

		memset(&soa_nsd_read, 0, sizeof(soa_nsd_read));
		memset(&soa_disk_read, 0, sizeof(soa_disk_read));
		memset(&soa_notified_read, 0, sizeof(soa_notified_read));

		if(!xfrd_read_check_str(in, "zone:") ||
		   !xfrd_read_check_str(in, "name:")  ||
		   !(p=xfrd_read_token(in)) ||
		   !(dname = dname_parse(tempregion, p)) ||
		   !xfrd_read_check_str(in, "state:") ||
		   !xfrd_read_i32(in, &state) || (state>2) ||
		   !xfrd_read_check_str(in, "master:") ||
		   !xfrd_read_i32(in, &masnum) ||
		   !xfrd_read_check_str(in, "next_master:") ||
		   !xfrd_read_i32(in, &nextmas) ||
		   !xfrd_read_check_str(in, "round_num:") ||
		   !xfrd_read_i32(in, &round_num) ||
		   !xfrd_read_check_str(in, "next_timeout:") ||
		   !xfrd_read_i32(in, &timeout) ||
		   !xfrd_read_check_str(in, "backoff:") ||
		   !xfrd_read_i32(in, &backoff) ||
		   !xfrd_read_state_soa(in, "soa_nsd_acquired:", "soa_nsd:",
			&soa_nsd_read, &soa_nsd_acquired_read) ||
		   !xfrd_read_state_soa(in, "soa_disk_acquired:", "soa_disk:",
			&soa_disk_read, &soa_disk_acquired_read) ||
		   !xfrd_read_state_soa(in, "soa_notify_acquired:", "soa_notify:",
			&soa_notified_read, &soa_notified_acquired_read))
		{
			log_msg(LOG_ERR, "xfrd: corrupt state file %s dated %d (now=%lld)",
				statefile, (int)filetime, (long long)xfrd_time());
			fclose(in);
			region_destroy(tempregion);
			return;
		}

		zone = (xfrd_zone_type*)rbtree_search(xfrd->zones, dname);
		if(!zone) {
			DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: state file has info for not configured zone %s", p));
			continue;
		}

		if(soa_nsd_acquired_read>xfrd_time()+15 ||
			soa_disk_acquired_read>xfrd_time()+15 ||
			soa_notified_acquired_read>xfrd_time()+15)
		{
			log_msg(LOG_ERR, "xfrd: statefile %s contains"
				" times in the future for zone %s. Ignoring.",
				statefile, zone->apex_str);
			continue;
		}
		zone->state = state;
		zone->master_num = masnum;
		zone->next_master = nextmas;
		zone->round_num = round_num;
		zone->timeout.tv_sec = timeout;
		zone->timeout.tv_usec = 0;
		zone->fresh_xfr_timeout = backoff*XFRD_TRANSFER_TIMEOUT_START;

		/* read the zone OK, now set the master properly */
		zone->master = acl_find_num(zone->zone_options->pattern->
			request_xfr, zone->master_num);
		if(!zone->master) {
			DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: primaries changed for zone %s",
				zone->apex_str));
			zone->master = zone->zone_options->pattern->request_xfr;
			zone->master_num = 0;
			zone->round_num = 0;
		}

		/*
		 * There is no timeout,
		 * or there is a notification,
		 * or there is a soa && current time is past refresh point
		 */
		soa_refresh = ntohl(soa_disk_read.refresh);
		if (soa_refresh > (time_t)zone->zone_options->pattern->max_refresh_time)
			soa_refresh = zone->zone_options->pattern->max_refresh_time;
		else if (soa_refresh < (time_t)zone->zone_options->pattern->min_refresh_time)
			soa_refresh = zone->zone_options->pattern->min_refresh_time;
		if(timeout == 0 || soa_notified_acquired_read != 0 ||
			(soa_disk_acquired_read != 0 &&
			(uint32_t)xfrd_time() - soa_disk_acquired_read
				> (uint32_t)soa_refresh))
		{
			zone->state = xfrd_zone_refreshing;
			xfrd_set_refresh_now(zone);
		}
		if(timeout != 0 && filetime + timeout < (uint32_t)xfrd_time()) {
			/* timeout is in the past, refresh the zone */
			timeout = 0;
			if(zone->state == xfrd_zone_ok)
				zone->state = xfrd_zone_refreshing;
			xfrd_set_refresh_now(zone);
		}

		/* There is a soa && current time is past expiry point */
		if(soa_disk_acquired_read!=0 &&
			(uint32_t)xfrd_time() - soa_disk_acquired_read
				> ntohl(soa_disk_read.expire))
		{
			zone->state = xfrd_zone_expired;
			xfrd_set_refresh_now(zone);
		} 

		/* there is a zone read and it matches what we had before */
		if(zone->soa_nsd_acquired && zone->state != xfrd_zone_expired
			&& zone->soa_nsd.serial == soa_nsd_read.serial) {
			xfrd_deactivate_zone(zone);
			zone->state = state;
			xfrd_set_timer(zone,
				within_refresh_bounds(zone, timeout));
		}	
		if((zone->soa_nsd_acquired == 0 && soa_nsd_acquired_read == 0 &&
			soa_disk_acquired_read == 0) ||
			(zone->state != xfrd_zone_ok && timeout != 0)) {
			/* but don't check now, because that would mean a
			 * storm of attempts on some master servers */
			xfrd_deactivate_zone(zone);
			zone->state = state;
			xfrd_set_timer(zone,
				within_retry_bounds(zone, timeout));
		}

		/* handle as an incoming SOA. */
		incoming_soa = zone->soa_nsd;
		incoming_acquired = zone->soa_nsd_acquired;
		zone->soa_nsd = soa_nsd_read;
		zone->soa_nsd_acquired = soa_nsd_acquired_read;
		/* use soa and soa_acquired from starting NSD, not what is stored in
		 * the state file, because the actual zone contents trumps the contents
		 * of this cache */
		zone->soa_disk = incoming_soa;
		zone->soa_disk_acquired = incoming_acquired;
		zone->soa_notified = soa_notified_read;
		zone->soa_notified_acquired = soa_notified_acquired_read;
		if (zone->state == xfrd_zone_expired)
		{
			xfrd_send_expire_notification(zone);
		}
		if(incoming_acquired != 0)
			xfrd_handle_incoming_soa(zone, &incoming_soa, incoming_acquired);
	}

	if(!xfrd_read_check_str(in, XFRD_FILE_MAGIC)) {
		log_msg(LOG_ERR, "xfrd: corrupt state file %s dated %d (now=%lld)",
			statefile, (int)filetime, (long long)xfrd_time());
		region_destroy(tempregion);
		fclose(in);
		return;
	}

	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: read %d zones from state file", (int)numzones));
	fclose(in);
	region_destroy(tempregion);
}

/* prints neato days hours and minutes. */
static void
neato_timeout(FILE* out, const char* str, time_t secs)
{
	fprintf(out, "%s", str);
	if(secs <= 0) {
		fprintf(out, " %llds", (long long)secs);
		return;
	}
	if(secs >= 3600*24) {
		fprintf(out, " %lldd", (long long)(secs/(3600*24)));
		secs = secs % (3600*24);
	}
	if(secs >= 3600) {
		fprintf(out, " %lldh", (long long)(secs/3600));
		secs = secs%3600;
	}
	if(secs >= 60) {
		fprintf(out, " %lldm", (long long)(secs/60));
		secs = secs%60;
	}
	if(secs > 0) {
		fprintf(out, " %llds", (long long)secs);
	}
}

static void xfrd_write_dname(FILE* out, uint8_t* dname)
{
	uint8_t* d= dname+1;
	uint8_t len = *d++;
	uint8_t i;

	if(dname[0]<=1) {
		fprintf(out, ".");
		return;
	}

	while(len)
	{
		assert(d - (dname+1) <= dname[0]);
		for(i=0; i<len; i++)
		{
			uint8_t ch = *d++;
			if (isalnum((unsigned char)ch) || ch == '-' || ch == '_') {
				fprintf(out, "%c", ch);
			} else if (ch == '.' || ch == '\\') {
				fprintf(out, "\\%c", ch);
			} else {
				fprintf(out, "\\%03u", (unsigned int)ch);
			}
		}
		fprintf(out, ".");
		len = *d++;
	}
}

static void
xfrd_write_state_soa(FILE* out, const char* id,
	xfrd_soa_type* soa, time_t soatime, const dname_type* ATTR_UNUSED(apex))
{
	fprintf(out, "\t%s_acquired: %d", id, (int)soatime);
	if(!soatime) {
		fprintf(out, "\n");
		return;
	}
	neato_timeout(out, "\t# was", xfrd_time()-soatime);
	fprintf(out, " ago\n");

	fprintf(out, "\t%s: %u %u %u %u", id,
		(unsigned)ntohs(soa->type), (unsigned)ntohs(soa->klass),
		(unsigned)ntohl(soa->ttl), (unsigned)ntohs(soa->rdata_count));
	fprintf(out, " ");
	xfrd_write_dname(out, soa->prim_ns);
	fprintf(out, " ");
	xfrd_write_dname(out, soa->email);
	fprintf(out, " %u", (unsigned)ntohl(soa->serial));
	fprintf(out, " %u", (unsigned)ntohl(soa->refresh));
	fprintf(out, " %u", (unsigned)ntohl(soa->retry));
	fprintf(out, " %u", (unsigned)ntohl(soa->expire));
	fprintf(out, " %u\n", (unsigned)ntohl(soa->minimum));
	fprintf(out, "\t#");
	neato_timeout(out, " refresh =", ntohl(soa->refresh));
	neato_timeout(out, " retry =", ntohl(soa->retry));
	neato_timeout(out, " expire =", ntohl(soa->expire));
	neato_timeout(out, " minimum =", ntohl(soa->minimum));
	fprintf(out, "\n");
}

void
xfrd_write_state(struct xfrd_state* xfrd)
{
	rbnode_type* p;
	const char* statefile = xfrd->nsd->options->xfrdfile;
	FILE *out;
	time_t now = xfrd_time();

	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: write file %s", statefile));
	out = fopen(statefile, "w");
	if(!out) {
		log_msg(LOG_ERR, "xfrd: Could not open file %s for writing: %s",
				statefile, strerror(errno));
		return;
	}

	fprintf(out, "%s\n", XFRD_FILE_MAGIC);
	fprintf(out, "# This file is written on exit by nsd xfr daemon.\n");
	fprintf(out, "# This file contains secondary zone information:\n");
	fprintf(out, "# 	* timeouts (when was zone data acquired)\n");
	fprintf(out, "# 	* state (OK, refreshing, expired)\n");
	fprintf(out, "# 	* which primary transfer to attempt next\n");
	fprintf(out, "# The file is read on start (but not on reload) by nsd xfr daemon.\n");
	fprintf(out, "# You can edit; but do not change statement order\n");
	fprintf(out, "# and no fancy stuff (like quoted \"strings\").\n");
	fprintf(out, "#\n");
	fprintf(out, "# If you remove a zone entry, it will be refreshed.\n");
	fprintf(out, "# This can be useful for an expired zone; it revives\n");
	fprintf(out, "# the zone temporarily, from refresh-expiry time.\n");
	fprintf(out, "# If you delete the file all secondary zones are updated.\n");
	fprintf(out, "#\n");
	fprintf(out, "# Note: if you edit this file while nsd is running,\n");
	fprintf(out, "#       it will be overwritten on exit by nsd.\n");
	fprintf(out, "\n");
	fprintf(out, "filetime: %lld\t# %s\n", (long long)now, ctime(&now));
	fprintf(out, "# The number of zone entries in this file\n");
	fprintf(out, "numzones: %d\n", (int)xfrd->zones->count);
	fprintf(out, "\n");
	for(p = rbtree_first(xfrd->zones); p && p!=RBTREE_NULL; p=rbtree_next(p))
	{
		xfrd_zone_type* zone = (xfrd_zone_type*)p;
		fprintf(out, "zone: \tname: %s\n", zone->apex_str);
		fprintf(out, "\tstate: %d", (int)zone->state);
		fprintf(out, " # %s", zone->state==xfrd_zone_ok?"OK":(
			zone->state==xfrd_zone_refreshing?"refreshing":"expired"));
		fprintf(out, "\n");
		fprintf(out, "\tmaster: %d\n", zone->master_num);
		fprintf(out, "\tnext_master: %d\n", zone->next_master);
		fprintf(out, "\tround_num: %d\n", zone->round_num);
		fprintf(out, "\tnext_timeout: %d",
			(zone->zone_handler_flags&EV_TIMEOUT)?(int)zone->timeout.tv_sec:0);
		if((zone->zone_handler_flags&EV_TIMEOUT)) {
			neato_timeout(out, "\t# =", zone->timeout.tv_sec);
		}
		fprintf(out, "\n");
		fprintf(out, "\tbackoff: %d\n", zone->fresh_xfr_timeout/XFRD_TRANSFER_TIMEOUT_START);
		xfrd_write_state_soa(out, "soa_nsd", &zone->soa_nsd,
			zone->soa_nsd_acquired, zone->apex);
		xfrd_write_state_soa(out, "soa_disk", &zone->soa_disk,
			zone->soa_disk_acquired, zone->apex);
		xfrd_write_state_soa(out, "soa_notify", &zone->soa_notified,
			zone->soa_notified_acquired, zone->apex);
		fprintf(out, "\n");
	}

	fprintf(out, "%s\n", XFRD_FILE_MAGIC);
	DEBUG(DEBUG_XFRD,1, (LOG_INFO, "xfrd: written %d zones to state file",
		(int)xfrd->zones->count));
	fclose(out);
}

/* return tempdirname */
static void
tempdirname(char* buf, size_t sz, struct nsd* nsd)
{
	snprintf(buf, sz, "%snsd-xfr-%d",
		nsd->options->xfrdir, (int)nsd->pid);
}

void
xfrd_make_tempdir(struct nsd* nsd)
{
	char tnm[1024];
	tempdirname(tnm, sizeof(tnm), nsd);
	/* create parent directories if needed (0750 permissions) */
	if(!create_dirs(tnm)) {
		log_msg(LOG_ERR, "parentdirs of %s failed", tnm);
	}
	/* restrictive permissions here, because this may be in /tmp */
	if(mkdir(tnm, 0700)==-1) {
		if(errno != EEXIST) {
			log_msg(LOG_ERR, "mkdir %s failed: %s",
				tnm, strerror(errno));
		}
	}
}

void
xfrd_del_tempdir(struct nsd* nsd)
{
	char tnm[1024];
	tempdirname(tnm, sizeof(tnm), nsd);
	/* ignore parent directories, they are likely /var/tmp, /tmp or
	 * /var/cache/nsd and do not have to be deleted */
	if(rmdir(tnm)==-1 && errno != ENOENT) {
		log_msg(LOG_WARNING, "rmdir %s failed: %s", tnm,
			strerror(errno));
	}
}

/* return name of xfrfile in tempdir */
static void
tempxfrname(char* buf, size_t sz, struct nsd* nsd, uint64_t number)
{
	char tnm[1024];
	tempdirname(tnm, sizeof(tnm), nsd);
	snprintf(buf, sz, "%s/xfr.%lld", tnm, (long long)number);
}

FILE*
xfrd_open_xfrfile(struct nsd* nsd, uint64_t number, char* mode)
{
	char fname[1200];
	FILE* xfr;
	tempxfrname(fname, sizeof(fname), nsd, number);
	xfr = fopen(fname, mode);
	if(!xfr && errno == ENOENT) {
		/* directory may not exist */
		xfrd_make_tempdir(nsd);
		xfr = fopen(fname, mode);
	}
	if(!xfr) {
		log_msg(LOG_ERR, "open %s for %s failed: %s", fname, mode,
			strerror(errno));
		return NULL;
	}
	return xfr;
}

void
xfrd_unlink_xfrfile(struct nsd* nsd, uint64_t number)
{
	char fname[1200];
	tempxfrname(fname, sizeof(fname), nsd, number);
	if(unlink(fname) == -1) {
		log_msg(LOG_WARNING, "could not unlink %s: %s", fname,
			strerror(errno));
	}
}

uint64_t
xfrd_get_xfrfile_size(struct nsd* nsd, uint64_t number )
{
	char fname[1200];
	struct stat tempxfr_stat;
	tempxfrname(fname, sizeof(fname), nsd, number);
	if( stat( fname, &tempxfr_stat ) < 0 ) {
	    log_msg(LOG_WARNING, "could not get file size %s: %s", fname,
		    strerror(errno));
	    return 0;
	}
	return (uint64_t)tempxfr_stat.st_size;
}
