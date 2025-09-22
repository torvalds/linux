/*
 * difffile.c - DIFF file handling source code. Read and write diff files.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include "difffile.h"
#include "xfrd-disk.h"
#include "util.h"
#include "packet.h"
#include "rdata.h"
#include "udb.h"
#include "nsec3.h"
#include "nsd.h"
#include "rrl.h"
#include "ixfr.h"
#include "zonec.h"
#include "xfrd-catalog-zones.h"

static int
write_64(FILE *out, uint64_t val)
{
	return write_data(out, &val, sizeof(val));
}

static int
write_32(FILE *out, uint32_t val)
{
	val = htonl(val);
	return write_data(out, &val, sizeof(val));
}

static int
write_8(FILE *out, uint8_t val)
{
	return write_data(out, &val, sizeof(val));
}

static int
write_str(FILE *out, const char* str)
{
	uint32_t len = strlen(str);
	if(!write_32(out, len))
		return 0;
	return write_data(out, str, len);
}

void
diff_write_packet(const char* zone, const char* pat, uint32_t old_serial,
	uint32_t new_serial, uint32_t seq_nr, uint8_t* data, size_t len,
	struct nsd* nsd, uint64_t filenumber)
{
	FILE* df = xfrd_open_xfrfile(nsd, filenumber, seq_nr?"a":"w");
	if(!df) {
		log_msg(LOG_ERR, "could not open transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		return;
	}

	/* if first part, first write the header */
	if(seq_nr == 0) {
		struct timeval tv;
		if (gettimeofday(&tv, NULL) != 0) {
			log_msg(LOG_ERR, "could not get timestamp for %s: %s",
				zone, strerror(errno));
		}
		if(!write_32(df, DIFF_PART_XFRF) ||
			!write_8(df, 0) /* notcommitted(yet) */ ||
			!write_32(df, 0) /* numberofparts when done */ ||
			!write_64(df, (uint64_t) tv.tv_sec) ||
			!write_32(df, (uint32_t) tv.tv_usec) ||
			!write_32(df, old_serial) ||
			!write_32(df, new_serial) ||
			!write_64(df, (uint64_t) tv.tv_sec) ||
			!write_32(df, (uint32_t) tv.tv_usec) ||
			!write_str(df, zone) ||
			!write_str(df, pat)) {
			log_msg(LOG_ERR, "could not write transfer %s file %lld: %s",
				zone, (long long)filenumber, strerror(errno));
			fclose(df);
			return;
		}
	}

	if(!write_32(df, DIFF_PART_XXFR) ||
		!write_32(df, len) ||
		!write_data(df, data, len) ||
		!write_32(df, len))
	{
		log_msg(LOG_ERR, "could not write transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
	}
	fclose(df);
}

void
diff_write_commit(const char* zone, uint32_t old_serial, uint32_t new_serial,
	uint32_t num_parts, uint8_t commit, const char* log_str,
	struct nsd* nsd, uint64_t filenumber)
{
	struct timeval tv;
	FILE* df;

	if (gettimeofday(&tv, NULL) != 0) {
		log_msg(LOG_ERR, "could not set timestamp for %s: %s",
			zone, strerror(errno));
	}

	/* overwrite the first part of the file with 'committed = 1', 
	 * as well as the end_time and number of parts.
	 * also write old_serial and new_serial, so that a bad file mixup
	 * will result in unusable serial numbers. */

	df = xfrd_open_xfrfile(nsd, filenumber, "r+");
	if(!df) {
		log_msg(LOG_ERR, "could not open transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		return;
	}
	if(!write_32(df, DIFF_PART_XFRF) ||
		!write_8(df, commit) /* committed */ ||
		!write_32(df, num_parts) ||
		!write_64(df, (uint64_t) tv.tv_sec) ||
		!write_32(df, (uint32_t) tv.tv_usec) ||
		!write_32(df, old_serial) ||
		!write_32(df, new_serial))
	{
		log_msg(LOG_ERR, "could not write transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		fclose(df);
		return;
	}

	/* append the log_str to the end of the file */
	if(fseek(df, 0, SEEK_END) == -1) {
		log_msg(LOG_ERR, "could not fseek transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		fclose(df);
		return;
	}
	if(!write_str(df, log_str)) {
		log_msg(LOG_ERR, "could not write transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		fclose(df);
		return;

	}
	fflush(df);
	fclose(df);
}

void
diff_update_commit(
	const char* zone, uint8_t commit, struct nsd* nsd, uint64_t filenumber)
{
	FILE *df;

	assert(zone != NULL);
	assert(nsd != NULL);
	assert(commit == DIFF_NOT_COMMITTED ||
	       commit == DIFF_COMMITTED ||
	       commit == DIFF_CORRUPT ||
	       commit == DIFF_INCONSISTENT ||
	       commit == DIFF_VERIFIED);

	df = xfrd_open_xfrfile(nsd, filenumber, "r+");
	if(!df) {
		log_msg(LOG_ERR, "could not open transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		return;
	}
	if(!write_32(df, DIFF_PART_XFRF) || !write_8(df, commit)) {
		log_msg(LOG_ERR, "could not write transfer %s file %lld: %s",
			zone, (long long)filenumber, strerror(errno));
		fclose(df);
		return;
	}
	fflush(df);
	fclose(df);
}

int
diff_read_64(FILE *in, uint64_t* result)
{
	if (fread(result, sizeof(*result), 1, in) == 1) {
		return 1;
	} else {
		return 0;
	}
}

int
diff_read_32(FILE *in, uint32_t* result)
{
	if (fread(result, sizeof(*result), 1, in) == 1) {
		*result = ntohl(*result);
		return 1;
	} else {
		return 0;
	}
}

int
diff_read_8(FILE *in, uint8_t* result)
{
	if (fread(result, sizeof(*result), 1, in) == 1) {
		return 1;
	} else {
		return 0;
	}
}

int
diff_read_str(FILE* in, char* buf, size_t len)
{
	uint32_t disklen;
	if(!diff_read_32(in, &disklen))
		return 0;
	if(disklen >= len)
		return 0;
	if(fread(buf, disklen, 1, in) != 1)
		return 0;
	buf[disklen] = 0;
	return 1;
}

static void
add_rdata_to_recyclebin(namedb_type* db, rr_type* rr)
{
	/* add rdatas to recycle bin. */
	size_t i;
	for(i=0; i<rr->rdata_count; i++)
	{
		if(!rdata_atom_is_domain(rr->type, i))
			region_recycle(db->region, rr->rdatas[i].data,
				rdata_atom_size(rr->rdatas[i])
				+ sizeof(uint16_t));
	}
	region_recycle(db->region, rr->rdatas,
		sizeof(rdata_atom_type)*rr->rdata_count);
}

/* this routine determines if below a domain there exist names with
 * data (is_existing) or no names below the domain have data.
 */
static int
has_data_below(domain_type* top)
{
	domain_type* d = top;
	assert(d != NULL);
	/* in the canonical ordering subdomains are after this name */
	d = domain_next(d);
	while(d != NULL && domain_is_subdomain(d, top)) {
		if(d->is_existing)
			return 1;
		d = domain_next(d);
	}
	return 0;
}

/** check if domain with 0 rrsets has become empty (nonexist) */
static domain_type*
rrset_zero_nonexist_check(domain_type* domain, domain_type* ce)
{
	/* is the node now an empty node (completely deleted) */
	if(domain->rrsets == 0) {
		/* if there is no data below it, it becomes non existing.
		   also empty nonterminals above it become nonexisting */
		/* check for data below this node. */
		if(!has_data_below(domain)) {
			/* nonexist this domain and all parent empty nonterminals */
			domain_type* p = domain;
			while(p != NULL && p->rrsets == 0) {
				if(p == ce || has_data_below(p))
					return p;
				p->is_existing = 0;
				/* fixup wildcard child of parent */
				if(p->parent &&
					p->parent->wildcard_child_closest_match == p)
					p->parent->wildcard_child_closest_match = domain_previous_existing_child(p);
				p = p->parent;
			}
		}
	}
	return NULL;
}

/** remove rrset.  Adjusts zone params.  Does not remove domain */
static void
rrset_delete(namedb_type* db, domain_type* domain, rrset_type* rrset)
{
	int i;
	/* find previous */
	rrset_type** pp = &domain->rrsets;
	while(*pp && *pp != rrset) {
		pp = &( (*pp)->next );
	}
	if(!*pp) {
		/* rrset does not exist for domain */
		return;
	}
	*pp = rrset->next;

	DEBUG(DEBUG_XFRD,2, (LOG_INFO, "delete rrset of %s type %s",
		domain_to_string(domain),
		rrtype_to_string(rrset_rrtype(rrset))));

	/* is this a SOA rrset ? */
	if(rrset->zone->soa_rrset == rrset) {
		rrset->zone->soa_rrset = 0;
	}
	if(rrset->zone->ns_rrset == rrset) {
		rrset->zone->ns_rrset = 0;
	}
	if(domain == rrset->zone->apex && rrset_rrtype(rrset) == TYPE_RRSIG) {
		for (i = 0; i < rrset->rr_count; ++i) {
			if(rr_rrsig_type_covered(&rrset->rrs[i])==TYPE_DNSKEY) {
				rrset->zone->is_secure = 0;
				break;
			}
		}
	}
	/* recycle the memory space of the rrset */
	for (i = 0; i < rrset->rr_count; ++i)
		add_rdata_to_recyclebin(db, &rrset->rrs[i]);
	region_recycle(db->region, rrset->rrs,
		sizeof(rr_type) * rrset->rr_count);
	rrset->rr_count = 0;
	region_recycle(db->region, rrset, sizeof(rrset_type));
}

static int
rdatas_equal(rdata_atom_type *a, rdata_atom_type *b, int num, uint16_t type,
	int* rdnum, char** reason)
{
	int k, start, end;
	start = 0;
	end = num;
	/**
	 * SOA RDATA comparisons in XFR are more lenient,
	 * only serial rdata is checked.
	 **/
	if (type == TYPE_SOA) {
		start = 2;
		end = 3;
	}
	for(k = start; k < end; k++)
	{
		if(rdata_atom_is_domain(type, k)) {
			if(dname_compare(domain_dname(a[k].domain),
				domain_dname(b[k].domain))!=0) {
				*rdnum = k;
				*reason = "dname data";
				return 0;
			}
		} else if(rdata_atom_is_literal_domain(type, k)) {
			/* literal dname, but compare case insensitive */
			if(a[k].data[0] != b[k].data[0]) {
				*rdnum = k;
				*reason = "literal dname len";
				return 0; /* uncompressed len must be equal*/
			}
			if(!dname_equal_nocase((uint8_t*)(a[k].data+1),
				(uint8_t*)(b[k].data+1), a[k].data[0])) {
				*rdnum = k;
				*reason = "literal dname data";
				return 0;
			}
		} else {
			/* check length */
			if(a[k].data[0] != b[k].data[0]) {
				*rdnum = k;
				*reason = "rdata len";
				return 0;
			}
			/* check data */
			if(memcmp(a[k].data+1, b[k].data+1, a[k].data[0])!=0) {
				*rdnum = k;
				*reason = "rdata data";
				return 0;
			}
		}
	}
	return 1;
}

static void
debug_find_rr_num(rrset_type* rrset, uint16_t type, uint16_t klass,
	rdata_atom_type *rdatas, ssize_t rdata_num)
{
	int i, rd;
	char* reason = "";

	for(i=0; i < rrset->rr_count; ++i) {
		if (rrset->rrs[i].type != type) {
			log_msg(LOG_WARNING, "diff: RR <%s, %s> does not match "
				"RR num %d type %s",
				dname_to_string(domain_dname(rrset->rrs[i].owner),0),
				rrtype_to_string(type),	i,
				rrtype_to_string(rrset->rrs[i].type));
		}
		if (rrset->rrs[i].klass != klass) {
			log_msg(LOG_WARNING, "diff: RR <%s, %s> class %d "
				"does not match RR num %d class %d",
				dname_to_string(domain_dname(rrset->rrs[i].owner),0),
				rrtype_to_string(type),
				klass, i,
				rrset->rrs[i].klass);
		}
		if (rrset->rrs[i].rdata_count != rdata_num) {
			log_msg(LOG_WARNING, "diff: RR <%s, %s> rdlen %u "
				"does not match RR num %d rdlen %d",
				dname_to_string(domain_dname(rrset->rrs[i].owner),0),
				rrtype_to_string(type),
				(unsigned) rdata_num, i,
				(unsigned) rrset->rrs[i].rdata_count);
		}
		if (!rdatas_equal(rdatas, rrset->rrs[i].rdatas, rdata_num, type,
			&rd, &reason)) {
			log_msg(LOG_WARNING, "diff: RR <%s, %s> rdata element "
				"%d differs from RR num %d rdata (%s)",
				dname_to_string(domain_dname(rrset->rrs[i].owner),0),
				rrtype_to_string(type),
				rd, i, reason);
		}
	}
}

static int
find_rr_num(rrset_type* rrset, uint16_t type, uint16_t klass,
	rdata_atom_type *rdatas, ssize_t rdata_num, int add)
{
	int i, rd;
	char* reason;

	for(i=0; i < rrset->rr_count; ++i) {
		if(rrset->rrs[i].type == type &&
		   rrset->rrs[i].klass == klass &&
		   rrset->rrs[i].rdata_count == rdata_num &&
		   rdatas_equal(rdatas, rrset->rrs[i].rdatas, rdata_num, type,
			&rd, &reason))
		{
			return i;
		}
	}
	/* this is odd. Log why rr cannot be found. */
	if (!add) {
		debug_find_rr_num(rrset, type, klass, rdatas, rdata_num);
	}
	return -1;
}

#ifdef NSEC3
/* see if nsec3 deletion triggers need action */
static void
nsec3_delete_rr_trigger(namedb_type* db, rr_type* rr, zone_type* zone)
{
	/* the RR has not actually been deleted yet, so we can inspect it */
	if(!zone->nsec3_param)
		return;
	/* see if the domain was an NSEC3-domain in the chain, but no longer */
	if(rr->type == TYPE_NSEC3 && rr->owner->nsec3 &&
		rr->owner->nsec3->nsec3_node.key &&
		nsec3_rr_uses_params(rr, zone) &&
		nsec3_in_chain_count(rr->owner, zone) <= 1) {
		domain_type* prev = nsec3_chain_find_prev(zone, rr->owner);
		/* remove from prehash because no longer an NSEC3 domain */
		if(domain_is_prehash(db->domains, rr->owner))
			prehash_del(db->domains, rr->owner);
		/* fixup the last in the zone */
		if(rr->owner == zone->nsec3_last)
			zone->nsec3_last = prev;
		/* unlink from the nsec3tree */
		zone_del_domain_in_hash_tree(zone->nsec3tree,
			&rr->owner->nsec3->nsec3_node);
		/* add previous NSEC3 to the prehash list */
		if(prev && prev != rr->owner)
			prehash_add(db->domains, prev);
		else	nsec3_clear_precompile(db, zone);
		/* this domain becomes ordinary data domain: done later */
	}
	/* see if the rr was NSEC3PARAM that we were using */
	else if(rr->type == TYPE_NSEC3PARAM && rr == zone->nsec3_param) {
		/* clear trees, wipe hashes, wipe precompile */
		nsec3_clear_precompile(db, zone);
		/* pick up new nsec3param (from udb, or avoid deleted rr) */
		nsec3_find_zone_param(db, zone, rr, 0);
		/* if no more NSEC3, done */
		if(!zone->nsec3_param)
			return;
		nsec3_precompile_newparam(db, zone);
	}
}

/* see if nsec3 prehash can be removed with new rrset content */
static void
nsec3_rrsets_changed_remove_prehash(domain_type* domain, zone_type* zone)
{
	/* deletion of rrset already done, we can check if conditions apply */
	/* see if the domain is no longer precompiled */
	/* it has a hash_node, but no longer fulfills conditions */
	if(nsec3_domain_part_of_zone(domain, zone) && domain->nsec3 &&
		domain->nsec3->hash_wc &&
		domain->nsec3->hash_wc->hash.node.key &&
		!nsec3_condition_hash(domain, zone)) {
		/* remove precompile */
		domain->nsec3->nsec3_cover = NULL;
		domain->nsec3->nsec3_wcard_child_cover = NULL;
		domain->nsec3->nsec3_is_exact = 0;
		/* remove it from the hash tree */
		zone_del_domain_in_hash_tree(zone->hashtree,
			&domain->nsec3->hash_wc->hash.node);
		zone_del_domain_in_hash_tree(zone->wchashtree,
			&domain->nsec3->hash_wc->wc.node);
	}
	if(domain != zone->apex && domain->nsec3 &&
		domain->nsec3->ds_parent_hash &&
		domain->nsec3->ds_parent_hash->node.key &&
		(!domain->parent || nsec3_domain_part_of_zone(domain->parent, zone)) &&
		!nsec3_condition_dshash(domain, zone)) {
		/* remove precompile */
		domain->nsec3->nsec3_ds_parent_cover = NULL;
		domain->nsec3->nsec3_ds_parent_is_exact = 0;
		/* remove it from the hash tree */
		zone_del_domain_in_hash_tree(zone->dshashtree,
			&domain->nsec3->ds_parent_hash->node);
	}
}

/* see if domain needs to get precompiled info */
static void
nsec3_rrsets_changed_add_prehash(namedb_type* db, domain_type* domain,
	zone_type* zone)
{
	if(!zone->nsec3_param)
		return;
	if((!domain->nsec3 || !domain->nsec3->hash_wc
	                   || !domain->nsec3->hash_wc->hash.node.key)
		&& nsec3_condition_hash(domain, zone)) {
		region_type* tmpregion = region_create(xalloc, free);
		nsec3_precompile_domain(db, domain, zone, tmpregion);
		region_destroy(tmpregion);
	}
	if((!domain->nsec3 || !domain->nsec3->ds_parent_hash
	                   || !domain->nsec3->ds_parent_hash->node.key)
		&& nsec3_condition_dshash(domain, zone)) {
		nsec3_precompile_domain_ds(db, domain, zone);
	}
}

/* see if nsec3 rrset-deletion triggers need action */
static void
nsec3_delete_rrset_trigger(namedb_type* db, domain_type* domain,
	zone_type* zone, uint16_t type)
{
	if(!zone->nsec3_param)
		return;
	nsec3_rrsets_changed_remove_prehash(domain, zone);
	/* for type nsec3, or a delegation, the domain may have become a
	 * 'normal' domain with its remaining data now */
	if(type == TYPE_NSEC3 || type == TYPE_NS || type == TYPE_DS)
		nsec3_rrsets_changed_add_prehash(db, domain, zone);
	/* for type DNAME or a delegation, obscured data may be revealed */
	if(type == TYPE_NS || type == TYPE_DS || type == TYPE_DNAME) {
		/* walk over subdomains and check them each */
		domain_type *d;
		for(d=domain_next(domain); d && domain_is_subdomain(d, domain);
			d=domain_next(d)) {
			nsec3_rrsets_changed_add_prehash(db, d, zone);
		}
	}
}

/* see if nsec3 addition triggers need action */
static void
nsec3_add_rr_trigger(namedb_type* db, rr_type* rr, zone_type* zone)
{
	/* the RR has been added in full, also to UDB (and thus NSEC3PARAM 
	 * in the udb has been adjusted) */
	if(zone->nsec3_param && rr->type == TYPE_NSEC3 &&
		(!rr->owner->nsec3 || !rr->owner->nsec3->nsec3_node.key)
		&& nsec3_rr_uses_params(rr, zone)) {
		if(!zone->nsec3_last) {
			/* all nsec3s have previously been deleted, but
			 * we have nsec3 parameters, set it up again from
			 * being cleared. */
			nsec3_precompile_newparam(db, zone);
		}
		/* added NSEC3 into the chain */
		nsec3_precompile_nsec3rr(db, rr->owner, zone);
		/* the domain has become an NSEC3-domain, if it was precompiled
		 * previously, remove that, neatly done in routine above */
		nsec3_rrsets_changed_remove_prehash(rr->owner, zone);
		/* set this NSEC3 to prehash */
		prehash_add(db->domains, rr->owner);
	} else if(!zone->nsec3_param && rr->type == TYPE_NSEC3PARAM) {
		/* see if this means NSEC3 chain can be used */
		nsec3_find_zone_param(db, zone, NULL, 0);
		if(!zone->nsec3_param)
			return;
		nsec3_zone_trees_create(db->region, zone);
		nsec3_precompile_newparam(db, zone);
	}
}

/* see if nsec3 rrset-addition triggers need action */
static void
nsec3_add_rrset_trigger(namedb_type* db, domain_type* domain, zone_type* zone,
	uint16_t type)
{
	/* the rrset has been added so we can inspect it */
	if(!zone->nsec3_param)
		return;
	/* because the rrset is added we can check conditions easily.
	 * check if domain needs to become precompiled now */
	nsec3_rrsets_changed_add_prehash(db, domain, zone);
	/* if a delegation, it changes from normal name to unhashed referral */
	if(type == TYPE_NS || type == TYPE_DS) {
		nsec3_rrsets_changed_remove_prehash(domain, zone);
	}
	/* if delegation or DNAME added, then some RRs may get obscured */
	if(type == TYPE_NS || type == TYPE_DS || type == TYPE_DNAME) {
		/* walk over subdomains and check them each */
		domain_type *d;
		for(d=domain_next(domain); d && domain_is_subdomain(d, domain);
			d=domain_next(d)) {
			nsec3_rrsets_changed_remove_prehash(d, zone);
		}
	}
}
#endif /* NSEC3 */

/* fixup usage lower for domain names in the rdata */
static void
rr_lower_usage(namedb_type* db, rr_type* rr)
{
	unsigned i;
	for(i=0; i<rr->rdata_count; i++) {
		if(rdata_atom_is_domain(rr->type, i)) {
			assert(rdata_atom_domain(rr->rdatas[i])->usage > 0);
			rdata_atom_domain(rr->rdatas[i])->usage --;
			if(rdata_atom_domain(rr->rdatas[i])->usage == 0)
				domain_table_deldomain(db,
					rdata_atom_domain(rr->rdatas[i]));
		}
	}
}

static void
rrset_lower_usage(namedb_type* db, rrset_type* rrset)
{
	unsigned i;
	for(i=0; i<rrset->rr_count; i++)
		rr_lower_usage(db, &rrset->rrs[i]);
}

int
delete_RR(namedb_type* db, const dname_type* dname,
	uint16_t type, uint16_t klass,
	buffer_type* packet, size_t rdatalen, zone_type *zone,
	region_type* temp_region, int* softfail)
{
	domain_type *domain;
	rrset_type *rrset;
	domain = domain_table_find(db->domains, dname);
	if(!domain) {
		log_msg(LOG_WARNING, "diff: domain %s does not exist",
			dname_to_string(dname,0));
		buffer_skip(packet, rdatalen);
		*softfail = 1;
		return 1; /* not fatal error */
	}
	rrset = domain_find_rrset(domain, zone, type);
	if(!rrset) {
		log_msg(LOG_WARNING, "diff: rrset %s does not exist",
			dname_to_string(dname,0));
		buffer_skip(packet, rdatalen);
		*softfail = 1;
		return 1; /* not fatal error */
	} else {
		/* find the RR in the rrset */
		domain_table_type *temptable;
		rdata_atom_type *rdatas;
		ssize_t rdata_num;
		int rrnum;
		temptable = domain_table_create(temp_region);
		/* This will ensure that the dnames in rdata are
		 * normalized, conform RFC 4035, section 6.2
		 */
		rdata_num = rdata_wireformat_to_rdata_atoms(
			temp_region, temptable, type, rdatalen, packet, &rdatas);
		if(rdata_num == -1) {
			log_msg(LOG_ERR, "diff: bad rdata for %s",
				dname_to_string(dname,0));
			return 0;
		}
		rrnum = find_rr_num(rrset, type, klass, rdatas, rdata_num, 0);
		if(rrnum == -1 && type == TYPE_SOA && domain == zone->apex
			&& rrset->rr_count != 0)
			rrnum = 0; /* replace existing SOA if no match */
		if(rrnum == -1) {
			log_msg(LOG_WARNING, "diff: RR <%s, %s> does not exist",
				dname_to_string(dname,0), rrtype_to_string(type));
			*softfail = 1;
			return 1; /* not fatal error */
		}
#ifdef NSEC3
		/* process triggers for RR deletions */
		nsec3_delete_rr_trigger(db, &rrset->rrs[rrnum], zone);
#endif
		/* lower usage (possibly deleting other domains, and thus
		 * invalidating the current RR's domain pointers) */
		rr_lower_usage(db, &rrset->rrs[rrnum]);
		if(rrset->rr_count == 1) {
			/* delete entire rrset */
			rrset_delete(db, domain, rrset);
			/* check if domain is now nonexisting (or parents) */
			rrset_zero_nonexist_check(domain, NULL);
#ifdef NSEC3
			/* cleanup nsec3 */
			nsec3_delete_rrset_trigger(db, domain, zone, type);
#endif
			/* see if the domain can be deleted (and inspect parents) */
			domain_table_deldomain(db, domain);
		} else {
			/* swap out the bad RR and decrease the count */
			rr_type* rrs_orig = rrset->rrs;
			add_rdata_to_recyclebin(db, &rrset->rrs[rrnum]);
			if(rrnum < rrset->rr_count-1)
				rrset->rrs[rrnum] = rrset->rrs[rrset->rr_count-1];
			memset(&rrset->rrs[rrset->rr_count-1], 0, sizeof(rr_type));
			/* realloc the rrs array one smaller */
			rrset->rrs = region_alloc_array_init(db->region, rrs_orig,
				(rrset->rr_count-1), sizeof(rr_type));
			if(!rrset->rrs) {
				log_msg(LOG_ERR, "out of memory, %s:%d", __FILE__, __LINE__);
				exit(1);
			}
			region_recycle(db->region, rrs_orig,
				sizeof(rr_type) * rrset->rr_count);
#ifdef NSEC3
			if(type == TYPE_NSEC3PARAM && zone->nsec3_param) {
				/* fixup nsec3_param pointer to same RR */
				assert(zone->nsec3_param >= rrs_orig &&
					zone->nsec3_param <=
					rrs_orig+rrset->rr_count);
				/* last moved to rrnum, others at same index*/
				if(zone->nsec3_param == &rrs_orig[
					rrset->rr_count-1])
					zone->nsec3_param = &rrset->rrs[rrnum];
				else
					zone->nsec3_param = (void*)
						((char*)zone->nsec3_param
						-(char*)rrs_orig +
						 (char*)rrset->rrs);
			}
#endif /* NSEC3 */
			rrset->rr_count --;
#ifdef NSEC3
			/* for type nsec3, the domain may have become a
			 * 'normal' domain with its remaining data now */
			if(type == TYPE_NSEC3)
				nsec3_rrsets_changed_add_prehash(db, domain,
					zone);
#endif /* NSEC3 */
		}
	}
	return 1;
}

int
add_RR(namedb_type* db, const dname_type* dname,
	uint16_t type, uint16_t klass, uint32_t ttl,
	buffer_type* packet, size_t rdatalen, zone_type *zone,
	int* softfail)
{
	domain_type* domain;
	rrset_type* rrset;
	rdata_atom_type *rdatas;
	rr_type *rrs_old;
	ssize_t rdata_num;
	int rrnum;
#ifdef NSEC3
	int rrset_added = 0;
#endif
	domain = domain_table_find(db->domains, dname);
	if(!domain) {
		/* create the domain */
		domain = domain_table_insert(db->domains, dname);
	}
	rrset = domain_find_rrset(domain, zone, type);
	if(!rrset) {
		/* create the rrset */
		rrset = region_alloc(db->region, sizeof(rrset_type));
		if(!rrset) {
			log_msg(LOG_ERR, "out of memory, %s:%d", __FILE__, __LINE__);
			exit(1);
		}
		rrset->zone = zone;
		rrset->rrs = 0;
		rrset->rr_count = 0;
		domain_add_rrset(domain, rrset);
#ifdef NSEC3
		rrset_added = 1;
#endif
	}

	/* dnames in rdata are normalized, conform RFC 4035,
	 * Section 6.2
	 */
	rdata_num = rdata_wireformat_to_rdata_atoms(
		db->region, db->domains, type, rdatalen, packet, &rdatas);
	if(rdata_num == -1) {
		log_msg(LOG_ERR, "diff: bad rdata for %s",
			dname_to_string(dname,0));
		return 0;
	}
	rrnum = find_rr_num(rrset, type, klass, rdatas, rdata_num, 1);
	if(rrnum != -1) {
		DEBUG(DEBUG_XFRD, 2, (LOG_ERR, "diff: RR <%s, %s> already exists",
			dname_to_string(dname,0), rrtype_to_string(type)));
		/* ignore already existing RR: lenient accepting of messages */
		*softfail = 1;
		return 1;
	}
	if(rrset->rr_count == 65535) {
		log_msg(LOG_ERR, "diff: too many RRs at %s",
			dname_to_string(dname,0));
		return 0;
	}

	/* re-alloc the rrs and add the new */
	rrs_old = rrset->rrs;
	rrset->rrs = region_alloc_array(db->region,
		(rrset->rr_count+1), sizeof(rr_type));
	if(!rrset->rrs) {
		log_msg(LOG_ERR, "out of memory, %s:%d", __FILE__, __LINE__);
		exit(1);
	}
	if(rrs_old)
		memcpy(rrset->rrs, rrs_old, rrset->rr_count * sizeof(rr_type));
	region_recycle(db->region, rrs_old, sizeof(rr_type) * rrset->rr_count);
	rrset->rr_count ++;

	rrset->rrs[rrset->rr_count - 1].owner = domain;
	rrset->rrs[rrset->rr_count - 1].rdatas = rdatas;
	rrset->rrs[rrset->rr_count - 1].ttl = ttl;
	rrset->rrs[rrset->rr_count - 1].type = type;
	rrset->rrs[rrset->rr_count - 1].klass = klass;
	rrset->rrs[rrset->rr_count - 1].rdata_count = rdata_num;

	/* see if it is a SOA */
	if(domain == zone->apex) {
		apex_rrset_checks(db, rrset, domain);
#ifdef NSEC3
		if(type == TYPE_NSEC3PARAM && zone->nsec3_param) {
			/* the pointer just changed, fix it up to point
			 * to the same record */
			assert(zone->nsec3_param >= rrs_old &&
				zone->nsec3_param < rrs_old+rrset->rr_count);
			/* in this order to make sure no overflow/underflow*/
			zone->nsec3_param = (void*)((char*)zone->nsec3_param - 
				(char*)rrs_old + (char*)rrset->rrs);
		}
#endif /* NSEC3 */
	}

#ifdef NSEC3
	if(rrset_added) {
		domain_type* p = domain->parent;
		nsec3_add_rrset_trigger(db, domain, zone, type);
		/* go up and process (possibly created) empty nonterminals, 
		 * until we hit the apex or root */
		while(p && p->rrsets == NULL && !p->is_apex) {
			nsec3_rrsets_changed_add_prehash(db, p, zone);
			p = p->parent;
		}
	}
	nsec3_add_rr_trigger(db, &rrset->rrs[rrset->rr_count - 1], zone);
#endif /* NSEC3 */
	return 1;
}

static zone_type*
find_or_create_zone(namedb_type* db, const dname_type* zone_name,
	struct nsd_options* opt, const char* zstr, const char* patname)
{
	zone_type* zone;
	struct zone_options* zopt;
	zone = namedb_find_zone(db, zone_name);
	if(zone) {
		return zone;
	}
	zopt = zone_options_find(opt, zone_name);
	if(!zopt) {
		/* if _implicit_ then insert as _part_of_config */
		if(strncmp(patname, PATTERN_IMPLICIT_MARKER,
			strlen(PATTERN_IMPLICIT_MARKER)) == 0) {
			zopt = zone_options_create(opt->region);
			if(!zopt) return 0;
			zopt->part_of_config = 1;
			zopt->name = region_strdup(opt->region, zstr);
			zopt->pattern = pattern_options_find(opt, patname);
			if(!zopt->name || !zopt->pattern) return 0;
			if(!nsd_options_insert_zone(opt, zopt)) {
				log_msg(LOG_ERR, "bad domain name or duplicate zone '%s' "
					"pattern %s", zstr, patname);
			}
		} else {
			/* create zone : presumably already added to zonelist
			 * by xfrd, who wrote the AXFR or IXFR to disk, so we only
			 * need to add it to our config.
			 * This process does not need linesize and offset zonelist */
			zopt = zone_list_zone_insert(opt, zstr, patname);
			if(!zopt)
				return 0;
		}
	}
	zone = namedb_zone_create(db, zone_name, zopt);
	return zone;
}

void
delete_zone_rrs(namedb_type* db, zone_type* zone)
{
	rrset_type *rrset;
	domain_type *domain = zone->apex, *next;
	int nonexist_check = 0;
	/* go through entire tree below the zone apex (incl subzones) */
	while(domain && domain_is_subdomain(domain, zone->apex))
	{
		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "delete zone visit %s",
			domain_to_string(domain)));
		/* delete all rrsets of the zone */
		while((rrset = domain_find_any_rrset(domain, zone))) {
			/* lower usage can delete other domains */
			rrset_lower_usage(db, rrset);
			/* rrset del does not delete our domain(yet) */
			rrset_delete(db, domain, rrset);
			/* no rrset_zero_nonexist_check, do that later */
			if(domain->rrsets == 0)
				nonexist_check = 1;
		}
		/* the delete upcoming could delete parents, but nothing next
		 * or after the domain so store next ptr */
		next = domain_next(domain);
		/* see if the domain can be deleted (and inspect parents) */
		domain_table_deldomain(db, domain);
		domain = next;
	}

	/* check if data deletions have created nonexisting domain entries,
	 * but after deleting domains so the checks are faster */
	if(nonexist_check) {
		domain_type* ce = NULL; /* for speeding up has_data_below */
		DEBUG(DEBUG_XFRD, 1, (LOG_INFO, "axfrdel: zero rrset check"));
		domain = zone->apex;
		while(domain && domain_is_subdomain(domain, zone->apex))
		{
			/* the interesting domains should be existing==1
			 * and rrsets==0, speeding up out processing of
			 * sub-zones, since we only spuriously check empty
			 * nonterminals */
			if(domain->is_existing)
				ce = rrset_zero_nonexist_check(domain, ce);
			domain = domain_next(domain);
		}
	}

	DEBUG(DEBUG_XFRD, 1, (LOG_INFO, "axfrdel: recyclebin holds %lu bytes",
		(unsigned long) region_get_recycle_size(db->region)));
#ifndef NDEBUG
	if(nsd_debug_level >= 2)
		region_log_stats(db->region);
#endif

	assert(zone->soa_rrset == 0);
	/* keep zone->soa_nx_rrset alloced: it is reused */
	assert(zone->ns_rrset == 0);
	assert(zone->is_secure == 0);
}

/* return value 0: syntaxerror,badIXFR, 1:OK, 2:done_and_skip_it */
static int
apply_ixfr(nsd_type* nsd, FILE *in, uint32_t serialno,
	uint32_t seq_nr, uint32_t seq_total,
	int* is_axfr, int* delete_mode, int* rr_count,
	struct zone* zone, uint64_t* bytes,
	int* softfail, struct ixfr_store* ixfr_store)
{
	uint32_t msglen, checklen, pkttype;
	int qcount, ancount;
	buffer_type* packet;
	region_type* region;

	/* note that errors could not really happen due to format of the
	 * packet since xfrd has checked all dnames and RRs before commit,
	 * this is why the errors are fatal (exit process), it must be
	 * something internal or a bad disk or something. */

	/* read ixfr packet RRs and apply to in memory db */
	if(!diff_read_32(in, &pkttype) || pkttype != DIFF_PART_XXFR) {
		log_msg(LOG_ERR, "could not read type or wrong type");
		return 0;
	}

	if(!diff_read_32(in, &msglen)) {
		log_msg(LOG_ERR, "could not read len");
		return 0;
	}

	if(msglen < QHEADERSZ) {
		log_msg(LOG_ERR, "msg too short");
		return 0;
	}

	region = region_create(xalloc, free);
	if(!region) {
		log_msg(LOG_ERR, "out of memory");
		return 0;
	}
	packet = buffer_create(region, QIOBUFSZ);
	if(msglen > QIOBUFSZ) {
		log_msg(LOG_ERR, "msg too long");
		region_destroy(region);
		return 0;
	}
	buffer_clear(packet);
	if(fread(buffer_begin(packet), msglen, 1, in) != 1) {
		log_msg(LOG_ERR, "short fread: %s", strerror(errno));
		region_destroy(region);
		return 0;
	}
	buffer_set_limit(packet, msglen);

	/* see if check on data fails: checks that we are not reading
	 * random garbage */
	if(!diff_read_32(in, &checklen) || checklen != msglen) {
		log_msg(LOG_ERR, "transfer part has incorrect checkvalue");
		return 0;
	}
	*bytes += msglen;

	/* only answer section is really used, question, additional and
	   authority section RRs are skipped */
	qcount = QDCOUNT(packet);
	ancount = ANCOUNT(packet);
	buffer_skip(packet, QHEADERSZ);
	/* qcount should be 0 or 1 really, ancount limited by 64k packet */
	if(qcount > 64 || ancount > 65530) {
		log_msg(LOG_ERR, "RR count impossibly high");
		region_destroy(region);
		return 0;
	}

	/* skip queries */
	for(int i=0; i < qcount; ++i) {
		if(!packet_skip_rr(packet, 1)) {
			log_msg(LOG_ERR, "bad RR in question section");
			region_destroy(region);
			return 0;
		}
	}

	DEBUG(DEBUG_XFRD, 2, (LOG_INFO, "diff: started packet for zone %s",
			domain_to_string(zone->apex)));

	for(int i=0; i < ancount; ++i, ++(*rr_count)) {
		const dname_type *owner;
		uint16_t type, klass, rrlen;
		uint32_t ttl;

		owner = dname_make_from_packet(region, packet, 1, 1);
		if(!owner) {
			log_msg(LOG_ERR, "bad xfr RR dname %d", *rr_count);
			region_destroy(region);
			return 0;
		}
		if(!buffer_available(packet, 10)) {
			log_msg(LOG_ERR, "bad xfr RR format %d", *rr_count);
			region_destroy(region);
			return 0;
		}
		type = buffer_read_u16(packet);
		klass = buffer_read_u16(packet);
		ttl = buffer_read_u32(packet);
		rrlen = buffer_read_u16(packet);
		if(!buffer_available(packet, rrlen)) {
			log_msg(LOG_ERR, "bad xfr RR rdata %d, len %d have %d",
				*rr_count, rrlen, (int)buffer_remaining(packet));
			region_destroy(region);
			return 0;
		}

		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "diff: %s parsed count %d, ax %d, delmode %d",
			domain_to_string(zone->apex), *rr_count, *is_axfr, *delete_mode));

		if (type == TYPE_SOA) {
			size_t position;
			uint32_t serial;
			position = buffer_position(packet);
			if (!packet_skip_dname(packet) ||
					!packet_skip_dname(packet) ||
					buffer_remaining(packet) < sizeof(uint32_t) * 5)
			{
				log_msg(LOG_ERR, "bad xfr SOA RR formerr.");
				region_destroy(region);
				return 0;
			}

			serial = buffer_read_u32(packet);
			buffer_set_position(packet, position);

			/* first RR: check if SOA and correct zone & serialno */
			if (*rr_count == 0) {
				assert(!*is_axfr);
				assert(!*delete_mode);
				if (klass != CLASS_IN) {
					log_msg(LOG_ERR, "first RR not SOA IN");
					region_destroy(region);
					return 0;
				}
				if(dname_compare(domain_dname(zone->apex), owner) != 0) {
					log_msg(LOG_ERR, "SOA dname not equal to zone %s",
						domain_to_string(zone->apex));
					region_destroy(region);
					return 0;
				}
				if(serial != serialno) {
					log_msg(LOG_ERR, "SOA serial %u different from commit %u",
						(unsigned)serial, (unsigned)serialno);
					region_destroy(region);
					return 0;
				}
				buffer_skip(packet, rrlen);

				if(ixfr_store)
					ixfr_store_add_newsoa(ixfr_store, ttl, packet, rrlen);

				continue;
			} else if (*rr_count == 1) {
				assert(!*is_axfr);
				assert(!*delete_mode);
				/* if the serial no of the SOA equals the serialno, then AXFR */
				if (serial == serialno)
					goto axfr;
				*delete_mode = 1;
				/* must have stuff in memory for a successful IXFR,
				 * the serial number of the SOA has been checked
				 * previously (by check_for_bad_serial) if it exists */
				if(!domain_find_rrset(zone->apex, zone, TYPE_SOA)) {
					log_msg(LOG_ERR, "%s SOA serial %u is not "
						"in memory, skip IXFR", domain_to_string(zone->apex), serialno);
					region_destroy(region);
					/* break out and stop the IXFR, ignore it */
					return 2;
				}

				if(ixfr_store)
					ixfr_store_add_oldsoa(ixfr_store, ttl, packet, rrlen);
			} else if (!*is_axfr) {
				/* do not delete final SOA RR for IXFR */
				if (i == ancount - 1 && seq_nr == seq_total - 1) {
					if (ixfr_store) {
						ixfr_store_add_newsoa(ixfr_store, ttl, packet, rrlen);
					}
					*delete_mode = 0;
					buffer_skip(packet, rrlen);
					continue;
				} else
					*delete_mode = !*delete_mode;

				if (ixfr_store && *delete_mode) {
					ixfr_store_add_newsoa(ixfr_store, ttl, packet, rrlen);
					ixfr_store_finish(ixfr_store, nsd, NULL);
					ixfr_store_start(zone, ixfr_store);
					ixfr_store_add_oldsoa(ixfr_store, ttl, packet, rrlen);
				}
				/* switch from delete-part to add-part and back again,
				   just before soa - so it gets deleted and added too */
				DEBUG(DEBUG_XFRD,2, (LOG_INFO, "diff: %s IXFRswapdel count %d, ax %d, delmode %d",
					domain_to_string(zone->apex), *rr_count, *is_axfr, *delete_mode));
			}
		} else {
			if (*rr_count == 0) {
				log_msg(LOG_ERR, "first RR not SOA IN");
				region_destroy(region);
				return 0;
			/* second RR: if not SOA: this is an AXFR; delete all zone contents */
			} else if (*rr_count == 1) {
axfr:
				*is_axfr = 1;
#ifdef NSEC3
				nsec3_clear_precompile(nsd->db, zone);
				zone->nsec3_param = NULL;
#endif
				delete_zone_rrs(nsd->db, zone);
				if(ixfr_store) {
					ixfr_store_cancel(ixfr_store);
					ixfr_store_delixfrs(zone);
				}
				DEBUG(DEBUG_XFRD,2, (LOG_INFO, "diff: %s sawAXFR count %d, ax %d, delmode %d",
					domain_to_string(zone->apex), *rr_count, *is_axfr, *delete_mode));
			}
		}

		if(type == TYPE_TSIG || type == TYPE_OPT) {
			/* ignore pseudo RRs */
			buffer_skip(packet, rrlen);
			continue;
		}

		DEBUG(DEBUG_XFRD,2, (LOG_INFO, "xfr %s RR dname is %s type %s",
			*delete_mode?"del":"add",
			dname_to_string(owner, 0), rrtype_to_string(type)));
		if(*delete_mode) {
			assert(!*is_axfr);
			/* delete this rr */
			if(ixfr_store)
				ixfr_store_delrr(ixfr_store, owner, type,
					klass, ttl, packet, rrlen, region);
			if(!delete_RR(nsd->db, owner, type, klass, packet,
				rrlen, zone, region, softfail)) {
				region_destroy(region);
				return 0;
			}
		} else {
			/* add this rr */
			if(ixfr_store)
				ixfr_store_addrr(ixfr_store, owner, type,
					klass, ttl, packet, rrlen, region);
			if(!add_RR(nsd->db, owner, type, klass, ttl, packet,
				rrlen, zone, softfail)) {
				region_destroy(region);
				return 0;
			}
		}
	}
	region_destroy(region);
	return 1;
}

static int
check_for_bad_serial(namedb_type* db, const char* zone_str, uint32_t old_serial)
{
	/* see if serial OK with in-memory serial */
	domain_type* domain;
	region_type* region = region_create(xalloc, free);
	const dname_type* zone_name = dname_parse(region, zone_str);
	zone_type* zone = 0;
	domain = domain_table_find(db->domains, zone_name);
	if(domain)
		zone = domain_find_zone(db, domain);
	if(zone && zone->apex == domain && zone->soa_rrset && old_serial)
	{
		uint32_t memserial;
		memcpy(&memserial, rdata_atom_data(zone->soa_rrset->rrs[0].rdatas[2]),
			sizeof(uint32_t));
		if(old_serial != ntohl(memserial)) {
			region_destroy(region);
			return 1;
		}
	}
	region_destroy(region);
	return 0;
}

int
apply_ixfr_for_zone(nsd_type* nsd, zone_type* zone, FILE* in,
	struct nsd_options* ATTR_UNUSED(opt), udb_base* taskudb, uint32_t xfrfilenr)
{
	char zone_buf[3072];
	char log_buf[5120];
	char patname_buf[2048];

	uint32_t old_serial, new_serial, num_parts, type;
	uint64_t time_end_0, time_start_0;
	uint32_t time_end_1, time_start_1;
	uint8_t committed;
	uint32_t i;
	uint64_t num_bytes = 0;
	assert(zone);

	/* read zone name and serial */
	if(!diff_read_32(in, &type)) {
		log_msg(LOG_ERR, "diff file too short");
		return 0;
	}
	if(type != DIFF_PART_XFRF) {
		log_msg(LOG_ERR, "xfr file has wrong format");
		return 0;

	}
	/* committed and num_parts are first because they need to be
	 * updated once the rest is written.  The log buf is not certain
	 * until its done, so at end of file.  The patname is in case a
	 * new zone is created, we know what the options-pattern is */
	if(!diff_read_8(in, &committed) ||
		!diff_read_32(in, &num_parts) ||
		!diff_read_64(in, &time_end_0) ||
		!diff_read_32(in, &time_end_1) ||
		!diff_read_32(in, &old_serial) ||
		!diff_read_32(in, &new_serial) ||
		!diff_read_64(in, &time_start_0) ||
		!diff_read_32(in, &time_start_1) ||
		!diff_read_str(in, zone_buf, sizeof(zone_buf)) ||
		!diff_read_str(in, patname_buf, sizeof(patname_buf))) {
		log_msg(LOG_ERR, "diff file bad commit part");
		return 0;
	}

	/* has been read in completely */
	if(strcmp(zone_buf, domain_to_string(zone->apex)) != 0) {
		log_msg(LOG_ERR, "file %s does not match task %s",
			zone_buf, domain_to_string(zone->apex));
		return 0;
	}
	switch(committed) {
	case DIFF_NOT_COMMITTED:
		log_msg(LOG_ERR, "diff file %s was not committed", zone_buf);
		return 0;
	case DIFF_CORRUPT:
		log_msg(LOG_ERR, "diff file %s was corrupt", zone_buf);
		return 0;
	case DIFF_INCONSISTENT:
		log_msg(LOG_ERR, "diff file %s was inconsistent", zone_buf);
		return 0;
	case DIFF_VERIFIED:
		log_msg(LOG_INFO, "diff file %s already verified", zone_buf);
		break;
	default:
		break;
	}
	if(num_parts == 0) {
		log_msg(LOG_ERR, "diff file %s was not completed", zone_buf);
		return 0;
	}
	if(check_for_bad_serial(nsd->db, zone_buf, old_serial)) {
		DEBUG(DEBUG_XFRD,1, (LOG_ERR,
			"skipping diff file commit with bad serial"));
		return -2; /* Success in "main" process, failure in "xfrd" */
	}

	if(!zone->is_skipped)
	{
		int is_axfr=0, delete_mode=0, rr_count=0, softfail=0;
		struct ixfr_store* ixfr_store = NULL, ixfr_store_mem;

		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "processing xfr: %s", zone_buf));
		if(zone_is_ixfr_enabled(zone))
			ixfr_store = ixfr_store_start(zone, &ixfr_store_mem);
		/* read and apply all of the parts */
		for(i=0; i<num_parts; i++) {
			int ret;
			DEBUG(DEBUG_XFRD,2, (LOG_INFO, "processing xfr: apply part %d", (int)i));
			ret = apply_ixfr(nsd, in, new_serial,
				i, num_parts, &is_axfr, &delete_mode,
				&rr_count, zone,
				&num_bytes, &softfail, ixfr_store);
			if(ret == 0) {
				log_msg(LOG_ERR, "bad ixfr packet part %d in diff file for %s", (int)i, zone_buf);
				diff_update_commit(
					zone_buf, DIFF_CORRUPT, nsd, xfrfilenr);
				/* the udb is still dirty, it is bad */
				return -1; /* Fatal! */
			} else if(ret == 2) {
				break;
			}
		}
		/* read the final log_str: but do not fail on it */
		if(!diff_read_str(in, log_buf, sizeof(log_buf))) {
			log_msg(LOG_ERR, "could not read log for transfer %s",
				zone_buf);
			snprintf(log_buf, sizeof(log_buf), "error reading log");
		}
#ifdef NSEC3
		prehash_zone(nsd->db, zone);
#endif /* NSEC3 */
		zone->is_changed = 1;
		zone->is_updated = 1;
		zone->is_checked = (committed == DIFF_VERIFIED);
		zone->mtime.tv_sec = time_end_0;
		zone->mtime.tv_nsec = time_end_1*1000;
		if(zone->logstr)
			region_recycle(nsd->db->region, zone->logstr,
				strlen(zone->logstr)+1);
		zone->logstr = region_strdup(nsd->db->region, log_buf);
		namedb_zone_free_filenames(nsd->db, zone);
		if(softfail && taskudb && !is_axfr) {
			log_msg(LOG_ERR, "Failed to apply IXFR cleanly "
				"(deletes nonexistent RRs, adds existing RRs). "
				"Zone %s contents is different from primary, "
				"starting AXFR. Transfer %s", zone_buf, log_buf);
			/* add/del failures in IXFR, get an AXFR */
			diff_update_commit(
				zone_buf, DIFF_INCONSISTENT, nsd, xfrfilenr);
			return -1; /* Fatal! */
		}
		if(ixfr_store)
			ixfr_store_finish(ixfr_store, nsd, log_buf);

		if(1 <= verbosity) {
			double elapsed = (double)(time_end_0 - time_start_0)+
				(double)((double)time_end_1
				-(double)time_start_1) / 1000000.0;
			VERBOSITY(1, (LOG_INFO, "zone %s %s of %"PRIu64" bytes in %g seconds",
				zone_buf, log_buf, num_bytes, elapsed));
		}
	}
	else {
		DEBUG(DEBUG_XFRD,1, (LOG_INFO, "skipping xfr: %s", zone_buf));
	}
	return 1;
}

static void udb_task_walk_chunk(void* base, void* d, uint64_t s, udb_walk_relptr_cb* cb, void *arg)
{
  struct task_list_d* p = (struct task_list_d*)d;
  assert(s >= p->size);
  (void)s;
  (*cb)(base, &p->next, arg);
}

void udb_walkfunc(void* base, void* warg, uint8_t t, void* d, uint64_t s,
  udb_walk_relptr_cb* cb, void *arg)
{
  (void)warg;
  switch(t) {
  case udb_chunk_type_task:
    udb_task_walk_chunk(base, d, s, cb, arg);
    break;
  default:
    /* no rel ptrs */
    break;
  }
}

struct udb_base* task_file_create(const char* file)
{
	return udb_base_create_new(file, &udb_walkfunc, NULL);
}

static int
task_create_new_elem(struct udb_base* udb, udb_ptr* last, udb_ptr* e,
	size_t sz, const dname_type* zname)
{
	if(!udb_ptr_alloc_space(e, udb, udb_chunk_type_task, sz)) {
		return 0;
	}
	if(udb_ptr_is_null(last)) {
		udb_base_set_userdata(udb, e->data);
	} else {
		udb_rptr_set_ptr(&TASKLIST(last)->next, udb, e);
	}
	udb_ptr_set_ptr(last, udb, e);

	/* fill in tasklist item */
	udb_rel_ptr_init(&TASKLIST(e)->next);
	TASKLIST(e)->size = sz;
	TASKLIST(e)->oldserial = 0;
	TASKLIST(e)->newserial = 0;
	TASKLIST(e)->yesno = 0;

	if(zname) {
		memmove(TASKLIST(e)->zname, zname, dname_total_size(zname));
	}
	return 1;
}

void task_new_soainfo(struct udb_base* udb, udb_ptr* last, struct zone* z,
	enum soainfo_hint hint)
{
	/* calculate size */
	udb_ptr e;
	size_t sz;
	const dname_type* apex, *ns, *em;
	if(!z || !z->apex || !domain_dname(z->apex))
		return; /* safety check */

	DEBUG(DEBUG_IPC,1, (LOG_INFO, "nsd: add soa info for zone %s",
		domain_to_string(z->apex)));
	apex = domain_dname(z->apex);
	sz = sizeof(struct task_list_d) + dname_total_size(apex);
	if(z->soa_rrset && hint == soainfo_ok) {
		ns = domain_dname(rdata_atom_domain(
			z->soa_rrset->rrs[0].rdatas[0]));
		em = domain_dname(rdata_atom_domain(
			z->soa_rrset->rrs[0].rdatas[1]));
		sz += sizeof(uint32_t)*6 + sizeof(uint8_t)*2
			+ ns->name_size + em->name_size;
	} else {
		ns = 0;
		em = 0;
	}

	/* create new task_list item */
	if(!task_create_new_elem(udb, last, &e, sz, apex)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add SOAINFO");
		return;
	}
	TASKLIST(&e)->task_type = task_soa_info;
	TASKLIST(&e)->yesno = (uint64_t)hint;

	if(z->soa_rrset && hint == soainfo_ok) {
		uint32_t ttl = htonl(z->soa_rrset->rrs[0].ttl);
		uint8_t* p = (uint8_t*)TASKLIST(&e)->zname;
		p += dname_total_size(apex);
		memmove(p, &ttl, sizeof(uint32_t));
		p += sizeof(uint32_t);
		memmove(p, &ns->name_size, sizeof(uint8_t));
		p += sizeof(uint8_t);
		memmove(p, dname_name(ns), ns->name_size);
		p += ns->name_size;
		memmove(p, &em->name_size, sizeof(uint8_t));
		p += sizeof(uint8_t);
		memmove(p, dname_name(em), em->name_size);
		p += em->name_size;
		memmove(p, rdata_atom_data(z->soa_rrset->rrs[0].rdatas[2]),
			sizeof(uint32_t));
		p += sizeof(uint32_t);
		memmove(p, rdata_atom_data(z->soa_rrset->rrs[0].rdatas[3]),
			sizeof(uint32_t));
		p += sizeof(uint32_t);
		memmove(p, rdata_atom_data(z->soa_rrset->rrs[0].rdatas[4]),
			sizeof(uint32_t));
		p += sizeof(uint32_t);
		memmove(p, rdata_atom_data(z->soa_rrset->rrs[0].rdatas[5]),
			sizeof(uint32_t));
		p += sizeof(uint32_t);
		memmove(p, rdata_atom_data(z->soa_rrset->rrs[0].rdatas[6]),
			sizeof(uint32_t));
	}
	udb_ptr_unlink(&e, udb);
}

void task_process_sync(struct udb_base* taskudb)
{
	/* need to sync before other process uses the mmap? */
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "task procsync %s size %d",
		taskudb->fname, (int)taskudb->base_size));
	(void)taskudb;
}

void task_remap(struct udb_base* taskudb)
{
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "task remap %s size %d",
		taskudb->fname, (int)taskudb->glob_data->fsize));
	udb_base_remap_process(taskudb);
}

void task_clear(struct udb_base* taskudb)
{
	udb_ptr t, n;
	udb_ptr_new(&t, taskudb, udb_base_get_userdata(taskudb));
	udb_base_set_userdata(taskudb, 0);
	udb_ptr_init(&n, taskudb);
	while(!udb_ptr_is_null(&t)) {
		udb_ptr_set_rptr(&n, taskudb, &TASKLIST(&t)->next);
		udb_rptr_zero(&TASKLIST(&t)->next, taskudb);
		udb_ptr_free_space(&t, taskudb, TASKLIST(&t)->size);
		udb_ptr_set_ptr(&t, taskudb, &n);
	}
	udb_ptr_unlink(&t, taskudb);
	udb_ptr_unlink(&n, taskudb);
}

void task_new_expire(struct udb_base* udb, udb_ptr* last,
	const struct dname* z, int expired)
{
	udb_ptr e;
	if(!z) return;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add expire info for zone %s",
		dname_to_string(z,NULL)));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)+
		dname_total_size(z), z)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add expire");
		return;
	}
	TASKLIST(&e)->task_type = task_expire;
	TASKLIST(&e)->yesno = expired;
	udb_ptr_unlink(&e, udb);
}

void task_new_check_zonefiles(udb_base* udb, udb_ptr* last,
	const dname_type* zone)
{
	udb_ptr e;
	xfrd_check_catalog_consumer_zonefiles(zone);
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task checkzonefiles"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d) +
		(zone?dname_total_size(zone):0), zone)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add check_zones");
		return;
	}
	TASKLIST(&e)->task_type = task_check_zonefiles;
	TASKLIST(&e)->yesno = (zone!=NULL);
	udb_ptr_unlink(&e, udb);
}

void task_new_write_zonefiles(udb_base* udb, udb_ptr* last,
	const dname_type* zone)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task writezonefiles"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d) +
		(zone?dname_total_size(zone):0), zone)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add writezones");
		return;
	}
	TASKLIST(&e)->task_type = task_write_zonefiles;
	TASKLIST(&e)->yesno = (zone!=NULL);
	udb_ptr_unlink(&e, udb);
}

void task_new_set_verbosity(udb_base* udb, udb_ptr* last, int v)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task set_verbosity"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d),
		NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add set_v");
		return;
	}
	TASKLIST(&e)->task_type = task_set_verbosity;
	TASKLIST(&e)->yesno = v;
	udb_ptr_unlink(&e, udb);
}

void
task_new_add_zone(udb_base* udb, udb_ptr* last, const char* zone,
	const char* pattern, unsigned zonestatid)
{
	size_t zlen = strlen(zone);
	size_t plen = strlen(pattern);
	void *p;
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task addzone %s %s", zone, pattern));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)+
		zlen + 1 + plen + 1, NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add addz");
		return;
	}
	TASKLIST(&e)->task_type = task_add_zone;
	TASKLIST(&e)->yesno = zonestatid;
	p = TASKLIST(&e)->zname;
	memcpy(p, zone, zlen+1);
	memmove((char*)p+zlen+1, pattern, plen+1);
	udb_ptr_unlink(&e, udb);
}

void
task_new_del_zone(udb_base* udb, udb_ptr* last, const dname_type* dname)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task delzone %s", dname_to_string(dname, 0)));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+dname_total_size(dname), dname)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add delz");
		return;
	}
	TASKLIST(&e)->task_type = task_del_zone;
	udb_ptr_unlink(&e, udb);
}

void task_new_add_key(udb_base* udb, udb_ptr* last, struct key_options* key)
{
	char* p;
	udb_ptr e;
	assert(key->name && key->algorithm && key->secret);
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task addkey"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+strlen(key->name)+1+strlen(key->algorithm)+1+
		strlen(key->secret)+1, NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add addk");
		return;
	}
	TASKLIST(&e)->task_type = task_add_key;
	p = (char*)TASKLIST(&e)->zname;
	memmove(p, key->name, strlen(key->name)+1);
	p+=strlen(key->name)+1;
	memmove(p, key->algorithm, strlen(key->algorithm)+1);
	p+=strlen(key->algorithm)+1;
	memmove(p, key->secret, strlen(key->secret)+1);
	udb_ptr_unlink(&e, udb);
}

void task_new_del_key(udb_base* udb, udb_ptr* last, const char* name)
{
	char* p;
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task delkey"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+strlen(name)+1, NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add delk");
		return;
	}
	TASKLIST(&e)->task_type = task_del_key;
	p = (char*)TASKLIST(&e)->zname;
	memmove(p, name, strlen(name)+1);
	udb_ptr_unlink(&e, udb);
}

void task_new_cookies(udb_base* udb, udb_ptr* last, int answer_cookie,
		size_t cookie_count, void* cookie_secrets) {
	udb_ptr e;
	char* p;
	size_t const secrets_size = sizeof(cookie_secrets_type);

	DEBUG(DEBUG_IPC, 1, (LOG_INFO, "add task cookies"));

	if(!task_create_new_elem(udb, last, &e,
			sizeof(struct task_list_d) + secrets_size, NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add cookies");
		return;
	}
	TASKLIST(&e)->task_type = task_cookies;
	TASKLIST(&e)->newserial = (uint32_t) answer_cookie;
	TASKLIST(&e)->yesno = (uint64_t) cookie_count;
	p = (char*)TASKLIST(&e)->zname;
	memmove(p, cookie_secrets, secrets_size);

	udb_ptr_unlink(&e, udb);
}

void task_new_add_pattern(udb_base* udb, udb_ptr* last,
	struct pattern_options* p)
{
	region_type* temp;
	buffer_type* buffer;
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task addpattern %s", p->pname));
	temp = region_create(xalloc, free);
	buffer = buffer_create(temp, 4096);
	pattern_options_marshal(buffer, p);
	buffer_flip(buffer);
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+ buffer_limit(buffer), NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add addp");
		region_destroy(temp);
		return;
	}
	TASKLIST(&e)->task_type = task_add_pattern;
	TASKLIST(&e)->yesno = buffer_limit(buffer);
	memmove(TASKLIST(&e)->zname, buffer_begin(buffer),
		buffer_limit(buffer));
	udb_ptr_unlink(&e, udb);
	region_destroy(temp);
}

void task_new_del_pattern(udb_base* udb, udb_ptr* last, const char* name)
{
	char* p;
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task delpattern %s", name));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+strlen(name)+1, NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add delp");
		return;
	}
	TASKLIST(&e)->task_type = task_del_pattern;
	p = (char*)TASKLIST(&e)->zname;
	memmove(p, name, strlen(name)+1);
	udb_ptr_unlink(&e, udb);
}

void task_new_opt_change(udb_base* udb, udb_ptr* last, struct nsd_options* opt)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task opt_change"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d),
		NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add o_c");
		return;
	}
	TASKLIST(&e)->task_type = task_opt_change;
#ifdef RATELIMIT
	TASKLIST(&e)->oldserial = opt->rrl_ratelimit;
	TASKLIST(&e)->newserial = opt->rrl_whitelist_ratelimit;
	TASKLIST(&e)->yesno = (uint64_t) opt->rrl_slip;
#else
	(void)opt;
#endif
	udb_ptr_unlink(&e, udb);
}

void task_new_zonestat_inc(udb_base* udb, udb_ptr* last, unsigned sz)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task zonestat_inc"));
	if(sz == 0)
		return; /* no need to decrease to 0 */
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d),
		NULL)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add z_i");
		return;
	}
	TASKLIST(&e)->task_type = task_zonestat_inc;
	TASKLIST(&e)->oldserial = (uint32_t)sz;
	udb_ptr_unlink(&e, udb);
}

int
task_new_apply_xfr(udb_base* udb, udb_ptr* last, const dname_type* dname,
	uint32_t old_serial, uint32_t new_serial, uint64_t filenumber)
{
	udb_ptr e;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "add task apply_xfr"));
	if(!task_create_new_elem(udb, last, &e, sizeof(struct task_list_d)
		+dname_total_size(dname), dname)) {
		log_msg(LOG_ERR, "tasklist: out of space, cannot add applyxfr");
		return 0;
	}
	TASKLIST(&e)->oldserial = old_serial;
	TASKLIST(&e)->newserial = new_serial;
	TASKLIST(&e)->yesno = filenumber;
	TASKLIST(&e)->task_type = task_apply_xfr;
	udb_ptr_unlink(&e, udb);
	return 1;
}

void
task_process_expire(namedb_type* db, struct task_list_d* task)
{
	uint8_t ok;
	zone_type* z = namedb_find_zone(db, task->zname);
	assert(task->task_type == task_expire);
	if(!z) {
		DEBUG(DEBUG_IPC, 1, (LOG_WARNING, "zone %s %s but not in zonetree",
			dname_to_string(task->zname, NULL),
			task->yesno?"expired":"unexpired"));
		return;
	}
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "xfrd: expire task zone %s %s",
		dname_to_string(task->zname,0),
		task->yesno?"expired":"unexpired"));
	/* find zone, set expire flag */
	ok = !task->yesno;
	/* only update zone->is_ok if needed to minimize copy-on-write
	 * of memory pages shared after fork() */
	if(ok && !z->is_ok)
		z->is_ok = 1;
	else if(!ok && z->is_ok)
		z->is_ok = 0;
}

static void
task_process_set_verbosity(struct task_list_d* task)
{
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "verbosity task %d", (int)task->yesno));
	verbosity = task->yesno;
}

static void
task_process_checkzones(struct nsd* nsd, udb_base* taskudb, udb_ptr* last_task,
	struct task_list_d* task)
{
	/* on SIGHUP check if zone-text-files changed and if so,
	 * reread.  When from xfrd-reload, no need to fstat the files */
	if(task->yesno) {
		struct zone_options* zo = zone_options_find(nsd->options,
			task->zname);
		if(zo)
			namedb_check_zonefile(nsd, taskudb, last_task, zo);
	} else {
		/* check all zones */
		namedb_check_zonefiles(nsd, nsd->options, taskudb, last_task);
	}
}

static void
task_process_writezones(struct nsd* nsd, struct task_list_d* task)
{
	if(task->yesno) {
		struct zone_options* zo = zone_options_find(nsd->options,
			task->zname);
		if(zo)
			namedb_write_zonefile(nsd, zo);
	} else {
		namedb_write_zonefiles(nsd, nsd->options);
	}
}

static void
task_process_add_zone(struct nsd* nsd, udb_base* udb, udb_ptr* last_task,
	struct task_list_d* task)
{
	zone_type* z;
	const dname_type* zdname;
	const char* zname = (const char*)task->zname;
	const char* pname = zname + strlen(zname)+1;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "addzone task %s %s", zname, pname));
	zdname = dname_parse(nsd->db->region, zname);
	if(!zdname) {
		log_msg(LOG_ERR, "can not parse zone name %s", zname);
		return;
	}
	/* create zone */
	z = find_or_create_zone(nsd->db, zdname, nsd->options, zname, pname);
	if(!z) {
		region_recycle(nsd->db->region, (void*)zdname,
			dname_total_size(zdname));
		log_msg(LOG_ERR, "can not add zone %s %s", zname, pname);
		return;
	}
	/* zdname is not used by the zone allocation. */
	region_recycle(nsd->db->region, (void*)zdname,
		dname_total_size(zdname));
	z->zonestatid = (unsigned)task->yesno;
	/* if zone is empty, attempt to read the zonefile from disk (if any) */
	if(!z->soa_rrset && z->opts->pattern->zonefile) {
		namedb_read_zonefile(nsd, z, udb, last_task);
	}
}

static void
task_process_del_zone(struct nsd* nsd, struct task_list_d* task)
{
	zone_type* zone;
	struct zone_options* zopt;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "delzone task %s", dname_to_string(
		task->zname, NULL)));
	zone = namedb_find_zone(nsd->db, task->zname);
	if(!zone)
		return;

#ifdef NSEC3
	nsec3_clear_precompile(nsd->db, zone);
	zone->nsec3_param = NULL;
#endif
	delete_zone_rrs(nsd->db, zone);

	/* remove from zonetree, apex, soa */
	zopt = zone->opts;
	namedb_zone_delete(nsd->db, zone);
	/* remove from options (zone_list already edited by xfrd) */
	zone_options_delete(nsd->options, zopt);
}

static void
task_process_add_key(struct nsd* nsd, struct task_list_d* task)
{
	struct key_options key;
	key.name = (char*)task->zname;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "addkey task %s", key.name));
	key.algorithm = key.name + strlen(key.name)+1;
	key.secret = key.algorithm + strlen(key.algorithm)+1;
	key_options_add_modify(nsd->options, &key);
	memset(key.secret, 0xdd, strlen(key.secret)); /* wipe secret */
}

static void
task_process_del_key(struct nsd* nsd, struct task_list_d* task)
{
	char* name = (char*)task->zname;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "delkey task %s", name));
	/* this is reload and nothing is using the TSIG key right now */
	key_options_remove(nsd->options, name);
}

static void
task_process_cookies(struct nsd* nsd, struct task_list_d* task) {
	DEBUG(DEBUG_IPC, 1, (LOG_INFO, "cookies task answer: %s, count: %d",
		task->newserial ? "yes" : "no", (int)task->yesno));
	
	nsd->do_answer_cookie = (int) task->newserial;
	nsd->cookie_count = (size_t) task->yesno;
	memmove(nsd->cookie_secrets, task->zname, sizeof(nsd->cookie_secrets));
	explicit_bzero(task->zname, sizeof(nsd->cookie_secrets));
}

static void
task_process_add_pattern(struct nsd* nsd, struct task_list_d* task)
{
	region_type* temp = region_create(xalloc, free);
	buffer_type buffer;
	struct pattern_options *pat;
	buffer_create_from(&buffer, task->zname, task->yesno);
	pat = pattern_options_unmarshal(temp, &buffer);
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "addpattern task %s", pat->pname));
	pattern_options_add_modify(nsd->options, pat);
	region_destroy(temp);
}

static void
task_process_del_pattern(struct nsd* nsd, struct task_list_d* task)
{
	char* name = (char*)task->zname;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "delpattern task %s", name));
	pattern_options_remove(nsd->options, name);
}

static void
task_process_opt_change(struct nsd* nsd, struct task_list_d* task)
{
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "optchange task"));
#ifdef RATELIMIT
	nsd->options->rrl_ratelimit = task->oldserial;
	nsd->options->rrl_whitelist_ratelimit = task->newserial;
	nsd->options->rrl_slip = task->yesno;
	rrl_set_limit(nsd->options->rrl_ratelimit, nsd->options->rrl_whitelist_ratelimit,
		nsd->options->rrl_slip);
#else
	(void)nsd; (void)task;
#endif
}

#ifdef USE_ZONE_STATS
static void
task_process_zonestat_inc(struct nsd* nsd, udb_base* udb, udb_ptr *last_task,
	struct task_list_d* task)
{
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "zonestat_inc task %u", (unsigned)task->oldserial));
	nsd->zonestatdesired = (unsigned)task->oldserial;
	/* send echo to xfrd to increment on its end */
	task_new_zonestat_inc(udb, last_task, nsd->zonestatdesired);
}
#endif

void
task_process_apply_xfr(struct nsd* nsd, udb_base* udb, udb_ptr* task)
{
	/* we have to use an udb_ptr task here, because the apply_xfr procedure
	 * appends soa_info which may remap and change the pointer. */
	zone_type* zone;
	FILE* df;
	DEBUG(DEBUG_IPC,1, (LOG_INFO, "applyxfr task %s", dname_to_string(
		TASKLIST(task)->zname, NULL)));
	zone = namedb_find_zone(nsd->db, TASKLIST(task)->zname);
	if(!zone) {
		/* assume the zone has been deleted and a zone transfer was
		 * still waiting to be processed */
		udb_ptr_free_space(task, udb, TASKLIST(task)->size);
		return;
	}

	/* apply the XFR */
	/* oldserial, newserial, yesno is filenumber */
	df = xfrd_open_xfrfile(nsd, TASKLIST(task)->yesno, "r");
	if(!df) {
		/* could not open file to update */
		/* soainfo_gone will be communicated from server_reload, unless
		   preceding updates have been applied */
		zone->is_skipped = 1;
		udb_ptr_free_space(task, udb, TASKLIST(task)->size);
		return;
	}
	/* read and apply zone transfer */
	switch(apply_ixfr_for_zone(nsd, zone, df, nsd->options, udb,
				TASKLIST(task)->yesno)) {
	case 1: /* Success */
		break;

	case 0: /* Failure */
		/* soainfo_gone will be communicated from server_reload, unless
		   preceding updates have been applied  */
		zone->is_skipped = 1;
		break;

	case -1:/* Fatal */
		exit(1);
		break;

	default:break;
	}
	fclose(df);
	udb_ptr_free_space(task, udb, TASKLIST(task)->size);
}


void task_process_in_reload(struct nsd* nsd, udb_base* udb, udb_ptr *last_task,
        udb_ptr* task)
{
	switch(TASKLIST(task)->task_type) {
	case task_expire:
		task_process_expire(nsd->db, TASKLIST(task));
		break;
	case task_check_zonefiles:
		task_process_checkzones(nsd, udb, last_task, TASKLIST(task));
		break;
	case task_write_zonefiles:
		task_process_writezones(nsd, TASKLIST(task));
		break;
	case task_set_verbosity:
		task_process_set_verbosity(TASKLIST(task));
		break;
	case task_add_zone:
		task_process_add_zone(nsd, udb, last_task, TASKLIST(task));
		break;
	case task_del_zone:
		task_process_del_zone(nsd, TASKLIST(task));
		break;
	case task_add_key:
		task_process_add_key(nsd, TASKLIST(task));
		break;
	case task_del_key:
		task_process_del_key(nsd, TASKLIST(task));
		break;
	case task_add_pattern:
		task_process_add_pattern(nsd, TASKLIST(task));
		break;
	case task_del_pattern:
		task_process_del_pattern(nsd, TASKLIST(task));
		break;
	case task_opt_change:
		task_process_opt_change(nsd, TASKLIST(task));
		break;
#ifdef USE_ZONE_STATS
	case task_zonestat_inc:
		task_process_zonestat_inc(nsd, udb, last_task, TASKLIST(task));
		break;
#endif
	case task_cookies:
		task_process_cookies(nsd, TASKLIST(task));
		break;
	default:
		log_msg(LOG_WARNING, "unhandled task in reload type %d",
			(int)TASKLIST(task)->task_type);
		break;
	}
	udb_ptr_free_space(task, udb, TASKLIST(task)->size);
}
