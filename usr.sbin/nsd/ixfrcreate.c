/*
 * ixfrcreate.c -- generating IXFR differences from zone files.
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include "ixfrcreate.h"
#include "namedb.h"
#include "ixfr.h"
#include "options.h"

/* spool a uint16_t to file */
static int spool_u16(FILE* out, uint16_t val)
{
	if(!fwrite(&val, sizeof(val), 1, out)) {
		return 0;
	}
	return 1;
}

/* spool a uint32_t to file */
static int spool_u32(FILE* out, uint32_t val)
{
	if(!fwrite(&val, sizeof(val), 1, out)) {
		return 0;
	}
	return 1;
}

/* spool dname to file */
static int spool_dname(FILE* out, dname_type* dname)
{
	uint16_t namelen = dname->name_size;
	if(!fwrite(&namelen, sizeof(namelen), 1, out)) {
		return 0;
	}
	if(!fwrite(dname_name(dname), namelen, 1, out)) {
		return 0;
	}
	return 1;
}

/* calculate the rdatalen of an RR */
static size_t rr_rdatalen_uncompressed(rr_type* rr)
{
	int i;
	size_t rdlen_uncompressed = 0;
	for(i=0; i<rr->rdata_count; i++) {
		if(rdata_atom_is_domain(rr->type, i)) {
			rdlen_uncompressed += domain_dname(rr->rdatas[i].domain)
				->name_size;
		} else {
			rdlen_uncompressed += rr->rdatas[i].data[0];
		}
	}
	return rdlen_uncompressed;
}

/* spool the data for one rr into the file */
static int spool_rr_data(FILE* out, rr_type* rr)
{
	int i;
	uint16_t rdlen;
	if(!spool_u32(out, rr->ttl))
		return 0;
	rdlen = rr_rdatalen_uncompressed(rr);
	if(!spool_u16(out, rdlen))
		return 0;
	for(i=0; i<rr->rdata_count; i++) {
		if(rdata_atom_is_domain(rr->type, i)) {
			if(!fwrite(dname_name(domain_dname(
				rr->rdatas[i].domain)), domain_dname(
				rr->rdatas[i].domain)->name_size, 1, out))
				return 0;
		} else {
			if(!fwrite(&rr->rdatas[i].data[1],
				rr->rdatas[i].data[0], 1, out))
				return 0;
		}
	}
	return 1;
}

/* spool one rrset to file */
static int spool_rrset(FILE* out, rrset_type* rrset)
{
	int i;
	if(rrset->rr_count == 0)
		return 1;
	if(!spool_u16(out, rrset->rrs[0].type))
		return 0;
	if(!spool_u16(out, rrset->rrs[0].klass))
		return 0;
	if(!spool_u16(out, rrset->rr_count))
		return 0;
	for(i=0; i<rrset->rr_count; i++) {
		if(!spool_rr_data(out, &rrset->rrs[i]))
			return 0;
	}
	return 1;
}

/* spool rrsets to file */
static int spool_rrsets(FILE* out, rrset_type* rrsets, struct zone* zone)
{
	rrset_type* s;
	for(s=rrsets; s; s=s->next) {
		if(s->zone != zone)
			continue;
		if(!spool_rrset(out, s)) {
			return 0;
		}
	}
	return 1;
}

/* count number of rrsets for a domain */
static size_t domain_count_rrsets(domain_type* domain, zone_type* zone)
{
	rrset_type* s;
	size_t count = 0;
	for(s=domain->rrsets; s; s=s->next) {
		if(s->zone == zone)
			count++;
	}
	return count;
}

/* spool the domain names to file, each one in turn. end with enddelimiter */
static int spool_domains(FILE* out, struct zone* zone)
{
	domain_type* domain;
	for(domain = zone->apex; domain && domain_is_subdomain(domain,
		zone->apex); domain = domain_next(domain)) {
		uint32_t count = domain_count_rrsets(domain, zone);
		if(count == 0)
			continue;
		/* write the name */
		if(!spool_dname(out, domain_dname(domain)))
			return 0;
		if(!spool_u32(out, count))
			return 0;
		/* write the rrsets */
		if(!spool_rrsets(out, domain->rrsets, zone))
			return 0;
	}
	/* the end delimiter is a 0 length. domain names are not zero length */
	if(!spool_u16(out, 0))
		return 0;
	return 1;
}

/* spool the namedb zone to the file. print error on failure. */
static int spool_zone_to_file(struct zone* zone, char* file_name,
	uint32_t serial)
{
	FILE* out;
	out = fopen(file_name, "w");
	if(!out) {
		log_msg(LOG_ERR, "could not open %s for writing: %s",
			file_name, strerror(errno));
		return 0;
	}
	if(!spool_dname(out, domain_dname(zone->apex))) {
		log_msg(LOG_ERR, "could not write %s: %s",
			file_name, strerror(errno));
		fclose(out);
		return 0;
	}
	if(!spool_u32(out, serial)) {
		log_msg(LOG_ERR, "could not write %s: %s",
			file_name, strerror(errno));
		fclose(out);
		return 0;
	}
	if(!spool_domains(out, zone)) {
		log_msg(LOG_ERR, "could not write %s: %s",
			file_name, strerror(errno));
		fclose(out);
		return 0;
	}
	fclose(out);
	return 1;
}

/* create ixfr spool file name */
static int create_ixfr_spool_name(struct ixfr_create* ixfrcr,
	const char* zfile)
{
	char buf[1024];
	snprintf(buf, sizeof(buf), "%s.spoolzone.%u", zfile,
		(unsigned)getpid());
	ixfrcr->file_name = strdup(buf);
	if(!ixfrcr->file_name)
		return 0;
	return 1;
}

/* start ixfr creation */
struct ixfr_create* ixfr_create_start(struct zone* zone, const char* zfile,
	uint64_t ixfr_size, int errorcmdline)
{
	struct ixfr_create* ixfrcr = (struct ixfr_create*)calloc(1,
		sizeof(*ixfrcr));
	if(!ixfrcr) {
		log_msg(LOG_ERR, "malloc failure");
		return NULL;
	}
	ixfrcr->zone_name_len = domain_dname(zone->apex)->name_size;
	ixfrcr->zone_name = (uint8_t*)malloc(ixfrcr->zone_name_len);
	if(!ixfrcr->zone_name) {
		free(ixfrcr);
		log_msg(LOG_ERR, "malloc failure");
		return NULL;
	}
	memmove(ixfrcr->zone_name, dname_name(domain_dname(zone->apex)),
		ixfrcr->zone_name_len);

	if(!create_ixfr_spool_name(ixfrcr, zfile)) {
		ixfr_create_free(ixfrcr);
		log_msg(LOG_ERR, "malloc failure");
		return NULL;
	}
	ixfrcr->old_serial = zone_get_current_serial(zone);
	if(!spool_zone_to_file(zone, ixfrcr->file_name, ixfrcr->old_serial)) {
		ixfr_create_free(ixfrcr);
		return NULL;
	}
	if(zone->opts && zone->opts->pattern)
		ixfrcr->max_size = (size_t)zone->opts->pattern->ixfr_size;
	else	ixfrcr->max_size = (size_t)ixfr_size;
	ixfrcr->errorcmdline = errorcmdline;
	return ixfrcr;
}

/* free ixfr create */
void ixfr_create_free(struct ixfr_create* ixfrcr)
{
	if(!ixfrcr)
		return;
	free(ixfrcr->file_name);
	free(ixfrcr->zone_name);
	free(ixfrcr);
}

/* read uint16_t from spool */
static int read_spool_u16(FILE* spool, uint16_t* val)
{
	if(fread(val, sizeof(*val), 1, spool) < 1)
		return 0;
	return 1;
}

/* read uint32_t from spool */
static int read_spool_u32(FILE* spool, uint32_t* val)
{
	if(fread(val, sizeof(*val), 1, spool) < 1)
		return 0;
	return 1;
}

/* read dname from spool */
static int read_spool_dname(FILE* spool, uint8_t* buf, size_t buflen,
	size_t* dname_len)
{
	uint16_t len;
	if(fread(&len, sizeof(len), 1, spool) < 1)
		return 0;
	if(len > buflen) {
		log_msg(LOG_ERR, "dname too long");
		return 0;
	}
	if(len > 0) {
		if(fread(buf, len, 1, spool) < 1)
			return 0;
	}
	*dname_len = len;
	return 1;
}

/* read and check the spool file header */
static int read_spool_header(FILE* spool, struct ixfr_create* ixfrcr)
{
	uint8_t dname[MAXDOMAINLEN+1];
	size_t dname_len;
	uint32_t serial;
	/* read apex */
	if(!read_spool_dname(spool, dname, sizeof(dname), &dname_len)) {
		log_msg(LOG_ERR, "error reading file %s: %s",
			ixfrcr->file_name, strerror(errno));
		return 0;
	}
	/* read serial */
	if(!read_spool_u32(spool, &serial)) {
		log_msg(LOG_ERR, "error reading file %s: %s",
			ixfrcr->file_name, strerror(errno));
		return 0;
	}

	/* check */
	if(ixfrcr->zone_name_len != dname_len ||
		memcmp(ixfrcr->zone_name, dname, ixfrcr->zone_name_len) != 0) {
		log_msg(LOG_ERR, "error file %s does not contain the correct zone apex",
			ixfrcr->file_name);
		return 0;
	}
	if(ixfrcr->old_serial != serial) {
		log_msg(LOG_ERR, "error file %s does not contain the correct zone serial",
			ixfrcr->file_name);
		return 0;
	}
	return 1;
}

/* store the old soa record when we encounter it on the spool */
static int process_store_oldsoa(struct ixfr_store* store, uint8_t* dname,
	size_t dname_len, uint16_t tp, uint16_t kl, uint32_t ttl, uint8_t* buf,
	uint16_t rdlen)
{
	if(store->data->oldsoa) {
		log_msg(LOG_ERR, "error spool contains multiple SOA records");
		return 0;
	}
	if(!ixfr_store_oldsoa_uncompressed(store, dname, dname_len, tp, kl,
		ttl, buf, rdlen)) {
		log_msg(LOG_ERR, "out of memory");
		return 0;
	}
	return 1;
}

/* see if rdata matches, true if equal */
static int rdata_match(struct rr* rr, uint8_t* rdata, uint16_t rdlen)
{
	size_t rdpos = 0;
	int i;
	for(i=0; i<rr->rdata_count; i++) {
		if(rdata_atom_is_domain(rr->type, i)) {
			if(rdpos + domain_dname(rr->rdatas[i].domain)->name_size
				> rdlen)
				return 0;
			if(memcmp(rdata+rdpos,
				dname_name(domain_dname(rr->rdatas[i].domain)),
				domain_dname(rr->rdatas[i].domain)->name_size)
				!= 0)
				return 0;
			rdpos += domain_dname(rr->rdatas[i].domain)->name_size;
		} else {
			if(rdpos + rr->rdatas[i].data[0] > rdlen)
				return 0;
			if(memcmp(rdata+rdpos, &rr->rdatas[i].data[1],
				rr->rdatas[i].data[0]) != 0)
				return 0;
			rdpos += rr->rdatas[i].data[0];
		}
	}
	if(rdpos != rdlen)
		return 0;
	return 1;
}

/* find an rdata in an rrset, true if found and sets index found */
static int rrset_find_rdata(struct rrset* rrset, uint32_t ttl, uint8_t* rdata,
	uint16_t rdlen, uint16_t* index)
{
	int i;
	for(i=0; i<rrset->rr_count; i++) {
		if(rrset->rrs[i].ttl != ttl)
			continue;
		if(rdata_match(&rrset->rrs[i], rdata, rdlen)) {
			*index = i;
			return 1;
		}
	}
	return 0;
}

/* sort comparison for uint16 elements */
static int sort_uint16(const void* x, const void* y)
{
	const uint16_t* ax = (const uint16_t*)x;
	const uint16_t* ay = (const uint16_t*)y;
	if(*ax < *ay)
		return -1;
	if(*ax > *ay)
		return 1;
	return 0;
}

/* spool read an rrset, it is a deleted RRset */
static int process_diff_rrset(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct domain* domain,
	uint16_t tp, uint16_t kl, uint16_t rrcount, struct rrset* rrset)
{
	/* read RRs from file and see if they are added, deleted or in both */
	uint8_t buf[MAX_RDLENGTH];
	uint16_t marked[65536];
	size_t marked_num = 0, atmarked;
	int i;
	for(i=0; i<rrcount; i++) {
		uint16_t rdlen, index;
		uint32_t ttl;
		if(!read_spool_u32(spool, &ttl) ||
		   !read_spool_u16(spool, &rdlen)) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		/* because rdlen is uint16_t always smaller than sizeof(buf)*/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
		assert(rdlen <= sizeof(buf));
#pragma GCC diagnostic pop
		if(fread(buf, rdlen, 1, spool) < 1) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		if(tp == TYPE_SOA) {
			if(!process_store_oldsoa(store,
				(void*)dname_name(domain_dname(domain)),
				domain_dname(domain)->name_size, tp, kl, ttl,
				buf, rdlen))
				return 0;
		}
		/* see if the rr is in the RRset */
		if(rrset_find_rdata(rrset, ttl, buf, rdlen, &index)) {
			/* it is in both, mark it */
			marked[marked_num++] = index;
		} else {
			/* not in new rrset, but only on spool, it is
			 * a deleted RR */
			if(!ixfr_store_delrr_uncompressed(store,
				(void*)dname_name(domain_dname(domain)),
				domain_dname(domain)->name_size,
				tp, kl, ttl, buf, rdlen)) {
				log_msg(LOG_ERR, "out of memory");
				return 0;
			}
		}
	}
	/* now that we are done, see if RRs in the rrset are not marked,
	 * and thus are new rrs that are added */
	qsort(marked, marked_num, sizeof(marked[0]), &sort_uint16);
	atmarked = 0;
	for(i=0; i<rrset->rr_count; i++) {
		if(atmarked < marked_num && marked[atmarked] == i) {
			/* the item is in the marked list, skip it */
			atmarked++;
			continue;
		}
		/* not in the marked list, the RR is added */
		if(!ixfr_store_addrr_rdatas(store, domain_dname(domain),
			rrset->rrs[i].type, rrset->rrs[i].klass,
			rrset->rrs[i].ttl, rrset->rrs[i].rdatas,
			rrset->rrs[i].rdata_count)) {
			log_msg(LOG_ERR, "out of memory");
			return 0;
		}
	}
	return 1;
}

/* spool read an rrset, it is a deleted RRset */
static int process_spool_delrrset(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, uint8_t* dname, size_t dname_len,
	uint16_t tp, uint16_t kl, uint16_t rrcount)
{
	/* read the RRs from file and add to del list. */
	uint8_t buf[MAX_RDLENGTH];
	int i;
	for(i=0; i<rrcount; i++) {
		uint16_t rdlen;
		uint32_t ttl;
		if(!read_spool_u32(spool, &ttl) ||
		   !read_spool_u16(spool, &rdlen)) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		/* because rdlen is uint16_t always smaller than sizeof(buf)*/
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
		assert(rdlen <= sizeof(buf));
#pragma GCC diagnostic pop
		if(fread(buf, rdlen, 1, spool) < 1) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		if(tp == TYPE_SOA) {
			if(!process_store_oldsoa(store, dname, dname_len,
				tp, kl, ttl, buf, rdlen))
				return 0;
		}
		if(!ixfr_store_delrr_uncompressed(store, dname, dname_len, tp,
			kl, ttl, buf, rdlen)) {
			log_msg(LOG_ERR, "out of memory");
			return 0;
		}
	}
	return 1;
}

/* add the rrset to the added list */
static int process_add_rrset(struct ixfr_store* ixfr_store,
	struct domain* domain, struct rrset* rrset)
{
	int i;
	for(i=0; i<rrset->rr_count; i++) {
		if(!ixfr_store_addrr_rdatas(ixfr_store, domain_dname(domain),
			rrset->rrs[i].type, rrset->rrs[i].klass,
			rrset->rrs[i].ttl, rrset->rrs[i].rdatas,
			rrset->rrs[i].rdata_count)) {
			log_msg(LOG_ERR, "out of memory");
			return 0;
		}
	}
	return 1;
}

/* add the RR types that are not in the marktypes list from the new zone */
static int process_marktypes(struct ixfr_store* store, struct zone* zone,
	struct domain* domain, uint16_t* marktypes, size_t marktypes_used)
{
	/* walk through the rrsets in the zone, if it is not in the
	 * marktypes list, then it is new and an added RRset */
	rrset_type* s;
	qsort(marktypes, marktypes_used, sizeof(marktypes[0]), &sort_uint16);
	for(s=domain->rrsets; s; s=s->next) {
		uint16_t tp;
		if(s->zone != zone)
			continue;
		tp = rrset_rrtype(s);
		if(bsearch(&tp, marktypes, marktypes_used, sizeof(marktypes[0]), &sort_uint16)) {
			/* the item is in the marked list, skip it */
			continue;
		}
		if(!process_add_rrset(store, domain, s))
			return 0;
	}
	return 1;
}

/* check the difference between the domain and RRs from spool */
static int process_diff_domain(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct zone* zone, struct domain* domain)
{
	/* Read the RR types from spool. Mark off the ones seen,
	 * later, the notseen ones from the new zone are added RRsets.
	 * For the ones not in the new zone, they are deleted RRsets.
	 * If they exist in old and new, check for RR differences. */
	uint32_t spool_type_count, i; 
	uint16_t marktypes[65536];
	size_t marktypes_used = 0;
	if(!read_spool_u32(spool, &spool_type_count)) {
		log_msg(LOG_ERR, "error reading file %s: %s",
			ixfrcr->file_name, strerror(errno));
		return 0;
	}
	if(spool_type_count > sizeof(marktypes)) {
		log_msg(LOG_ERR, "error reading file %s: spool type count "
			"too large", ixfrcr->file_name);
		return 0;
	}
	for(i=0; i<spool_type_count; i++) {
		uint16_t tp, kl, rrcount;
		struct rrset* rrset;
		if(!read_spool_u16(spool, &tp) ||
		   !read_spool_u16(spool, &kl) ||
		   !read_spool_u16(spool, &rrcount)) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		/* The rrcount is within limits of sizeof(marktypes), because
		 * the uint16_t < 65536 */
		rrset = domain_find_rrset(domain, zone, tp);
		if(!rrset) {
			/* rrset in spool but not in new zone, deleted RRset */
			if(!process_spool_delrrset(spool, ixfrcr, store,
				(void*)dname_name(domain_dname(domain)),
				domain_dname(domain)->name_size, tp, kl,
				rrcount))
				return 0;
		} else {
			/* add to the marked types, this one is present in
			 * spool */
			marktypes[marktypes_used++] = tp;
			/* rrset in old and in new zone, diff the RRset */
			if(!process_diff_rrset(spool, ixfrcr, store, domain,
				tp, kl, rrcount, rrset))
				return 0;
		}
	}
	/* process markoff to see if new zone has RRsets not in spool,
	 * those are added RRsets. */
	if(!process_marktypes(store, zone, domain, marktypes, marktypes_used))
		return 0;
	return 1;
}

/* add the RRs for the domain in new zone */
static int process_domain_add_RRs(struct ixfr_store* store, struct zone* zone,
	struct domain* domain)
{
	rrset_type* s;
	for(s=domain->rrsets; s; s=s->next) {
		if(s->zone != zone)
			continue;
		if(!process_add_rrset(store, domain, s))
			return 0;
	}
	return 1;
}

/* del the RRs for the domain from the spool */
static int process_domain_del_RRs(struct ixfr_create* ixfrcr,
	struct ixfr_store* store, FILE* spool, uint8_t* dname,
	size_t dname_len)
{
	uint32_t spool_type_count, i;
	if(!read_spool_u32(spool, &spool_type_count)) {
		log_msg(LOG_ERR, "error reading file %s: %s",
			ixfrcr->file_name, strerror(errno));
		return 0;
	}
	if(spool_type_count > 65536) {
		log_msg(LOG_ERR, "error reading file %s: del RR spool type "
			"count too large", ixfrcr->file_name);
		return 0;
	}
	for(i=0; i<spool_type_count; i++) {
		uint16_t tp, kl, rrcount;
		if(!read_spool_u16(spool, &tp) ||
		   !read_spool_u16(spool, &kl) ||
		   !read_spool_u16(spool, &rrcount)) {
			log_msg(LOG_ERR, "error reading file %s: %s",
				ixfrcr->file_name, strerror(errno));
			return 0;
		}
		/* The rrcount is within reasonable limits, because
		 * the uint16_t < 65536 */
		if(!process_spool_delrrset(spool, ixfrcr, store, dname,
			dname_len, tp, kl, rrcount))
			return 0;
	}
	return 1;
}

/* init the spool dname iterator */
static void spool_dname_iter_init(struct spool_dname_iterator* iter,
	FILE* spool, char* file_name)
{
	memset(iter, 0, sizeof(*iter));
	iter->spool = spool;
	iter->file_name = file_name;
}

/* read the dname element into the buffer for the spool dname iterator */
static int spool_dname_iter_read(struct spool_dname_iterator* iter)
{
	if(!read_spool_dname(iter->spool, iter->dname, sizeof(iter->dname),
		&iter->dname_len)) {
		log_msg(LOG_ERR, "error reading file %s: %s",
			iter->file_name, strerror(errno));
		return 0;
	}
	return 1;
}

/* get the next name to operate on, that is not processed yet, 0 on failure
 * returns okay on endoffile, check with eof for that.
 * when done with an element, set iter->is_processed on the element. */
static int spool_dname_iter_next(struct spool_dname_iterator* iter)
{
	if(iter->eof)
		return 1;
	if(!iter->read_first) {
		/* read the first one */
		if(!spool_dname_iter_read(iter))
			return 0;
		if(iter->dname_len == 0)
			iter->eof = 1;
		iter->read_first = 1;
		iter->is_processed = 0;
	}
	if(!iter->is_processed) {
		/* the current one needs processing */
		return 1;
	}
	/* read the next one */
	if(!spool_dname_iter_read(iter))
		return 0;
	if(iter->dname_len == 0)
		iter->eof = 1;
	iter->is_processed = 0;
	return 1;
}

/* check if the ixfr is too large */
static int ixfr_create_too_large(struct ixfr_create* ixfrcr,
	struct ixfr_store* store)
{
	if(store->cancelled)
		return 1;
	if(ixfrcr->max_size != 0 &&
		ixfr_data_size(store->data) > ixfrcr->max_size) {
		if(ixfrcr->errorcmdline) {
			log_msg(LOG_ERR, "the ixfr for %s exceeds size %u, it is not created",
				wiredname2str(ixfrcr->zone_name),
				(unsigned)ixfrcr->max_size);
		} else {
			VERBOSITY(2, (LOG_INFO, "the ixfr for %s exceeds size %u, it is not created",
				wiredname2str(ixfrcr->zone_name),
				(unsigned)ixfrcr->max_size));
		}
		ixfr_store_cancel(store);
		return 1;
	}
	return 0;
}

/* process the spool input before the domain */
static int process_spool_before_domain(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct domain* domain,
	struct spool_dname_iterator* iter, struct region* tmp_region)
{
	const dname_type* dname;
	if(ixfr_create_too_large(ixfrcr, store))
		return 0;
	/* read the domains and rrsets before the domain and those are from
	 * the old zone. If the domain is equal, return to have that processed
	 * if we bypass, that means the domain does not exist, do that */
	while(!iter->eof) {
		if(!spool_dname_iter_next(iter))
			return 0;
		if(iter->eof)
			break;
		/* see if we are at, before or after the domain */
		dname = dname_make(tmp_region, iter->dname, 1);
		if(!dname) {
			log_msg(LOG_ERR, "error in dname in %s",
				iter->file_name);
			return 0;
		}
		if(dname_compare(dname, domain_dname(domain)) < 0) {
			/* the dname is smaller than the one from the zone.
			 * it must be deleted, process it */
			if(!process_domain_del_RRs(ixfrcr, store, spool,
				iter->dname, iter->dname_len))
				return 0;
			iter->is_processed = 1;
		} else {
			/* we are at or after the domain we are looking for,
			 * done here */
			return 1;
		}
		if(ixfr_create_too_large(ixfrcr, store))
			return 0;
	}
	/* no more domains on spool, done here */
	return 1;
}

/* process the spool input for the domain */
static int process_spool_for_domain(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct zone* zone, struct domain* domain,
	struct spool_dname_iterator* iter, struct region* tmp_region)
{
	/* process all the spool that is not the domain, that is before the
	 * domain in the new zone */
	if(!process_spool_before_domain(spool, ixfrcr, store, domain, iter,
		tmp_region))
		return 0;
	
	if(ixfr_create_too_large(ixfrcr, store))
		return 0;
	/* are we at the correct domain now? */
	if(iter->eof || iter->dname_len != domain_dname(domain)->name_size ||
		memcmp(iter->dname, dname_name(domain_dname(domain)),
			iter->dname_len) != 0) {
		/* the domain from the new zone is not present in the old zone,
		 * the content is in the added RRs set */
		if(!process_domain_add_RRs(store, zone, domain))
			return 0;
		return 1;
	}

	/* process the domain */
	/* the domain exists both in the old and new zone,
	 * check for RR differences */
	if(!process_diff_domain(spool, ixfrcr, store, zone, domain))
		return 0;
	iter->is_processed = 1;

	return 1;
}

/* process remaining spool items */
static int process_spool_remaining(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct spool_dname_iterator* iter)
{
	/* the remaining domain names in the spool file, that is after
	 * the last domain in the new zone. */
	if(ixfr_create_too_large(ixfrcr, store))
		return 0;
	while(!iter->eof) {
		if(!spool_dname_iter_next(iter))
			return 0;
		if(iter->eof)
			break;
		/* the domain only exists in the spool, the old zone,
		 * and not in the new zone. That would be domains
		 * after the new zone domains, or there are no new
		 * zone domains */
		if(!process_domain_del_RRs(ixfrcr, store, spool, iter->dname,
			iter->dname_len))
			return 0;
		iter->is_processed = 1;
		if(ixfr_create_too_large(ixfrcr, store))
			return 0;
	}
	return 1;
}

/* walk through the zone and find the differences */
static int ixfr_create_walk_zone(FILE* spool, struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct zone* zone)
{
	struct domain* domain;
	struct spool_dname_iterator iter;
	struct region* tmp_region;
	spool_dname_iter_init(&iter, spool, ixfrcr->file_name);
	tmp_region = region_create(xalloc, free);
	for(domain = zone->apex; domain && domain_is_subdomain(domain,
		zone->apex); domain = domain_next(domain)) {
		uint32_t count = domain_count_rrsets(domain, zone);
		if(count == 0)
			continue;

		/* the domain is a domain in the new zone */
		if(!process_spool_for_domain(spool, ixfrcr, store, zone,
			domain, &iter, tmp_region)) {
			region_destroy(tmp_region);
			return 0;
		}
		region_free_all(tmp_region);
		if(ixfr_create_too_large(ixfrcr, store))
			return 0;
	}
	if(!process_spool_remaining(spool, ixfrcr, store, &iter)) {
		region_destroy(tmp_region);
		return 0;
	}
	region_destroy(tmp_region);
	return 1;
}

/* see if the ixfr has already been created by reading the file header
 * of the to-be-created file, if that file already exists */
static int ixfr_create_already_done_serial(struct zone* zone,
	const char* zfile, int checknew, uint32_t old_serial,
	uint32_t new_serial)
{
	uint32_t file_oldserial = 0, file_newserial = 0;
	size_t data_size = 0;
	if(!ixfr_read_file_header(zone->opts->name, zfile, 1, &file_oldserial,
		&file_newserial, &data_size, 0)) {
		/* could not read, so it was not done */
		return 0;
	}
	if(file_oldserial == old_serial &&
		(!checknew || file_newserial == new_serial)) {
		log_msg(LOG_INFO, "IXFR already exists in file %s.ixfr, nothing to do",
			zfile);
		return 1;
	}
	return 0;
}

/* See the data size of the ixfr by reading the file header of the ixfr file */
static int ixfr_read_header_data_size(const char* zname,
	const char* zfile, int file_num, size_t* data_size)
{
	uint32_t file_oldserial = 0, file_newserial = 0;
	if(!ixfr_read_file_header(zname, zfile, file_num, &file_oldserial,
		&file_newserial, data_size, 0)) {
		/* could not read */
		return 0;
	}
	return 1;
}

/* see if the ixfr has already been created by reading the file header
 * of the to-be-created file, if that file already exists */
static int ixfr_create_already_done(struct ixfr_create* ixfrcr,
	struct zone* zone, const char* zfile, int checknew)
{
	return ixfr_create_already_done_serial(zone, zfile, checknew,
		ixfrcr->old_serial, ixfrcr->new_serial);
}

/* store the new soa record for the ixfr */
static int ixfr_create_store_newsoa(struct ixfr_store* store,
	struct zone* zone)
{
	if(!zone || !zone->soa_rrset) {
		log_msg(LOG_ERR, "error no SOA rrset");
		return 0;
	}
	if(zone->soa_rrset->rr_count == 0) {
		log_msg(LOG_ERR, "error empty SOA rrset");
		return 0;
	}
	if(!ixfr_store_add_newsoa_rdatas(store, domain_dname(zone->apex),
		zone->soa_rrset->rrs[0].type, zone->soa_rrset->rrs[0].klass,
		zone->soa_rrset->rrs[0].ttl, zone->soa_rrset->rrs[0].rdatas,
		zone->soa_rrset->rrs[0].rdata_count)) {
		log_msg(LOG_ERR, "out of memory");
		return 0;
	}
	return 1;
}

/* initialise ixfr_create perform, open spool, read header, get serial */
static int ixfr_perform_init(struct ixfr_create* ixfrcr, struct zone* zone,
	struct ixfr_store* store_mem, struct ixfr_store** store, FILE** spool)
{
	*spool = fopen(ixfrcr->file_name, "r");
	if(!*spool) {
		log_msg(LOG_ERR, "could not open %s for reading: %s",
			ixfrcr->file_name, strerror(errno));
		return 0;
	}
	if(!read_spool_header(*spool, ixfrcr)) {
		fclose(*spool);
		return 0;
	}
	ixfrcr->new_serial = zone_get_current_serial(zone);
	*store = ixfr_store_start(zone, store_mem);
	if(!ixfr_create_store_newsoa(*store, zone)) {
		fclose(*spool);
		ixfr_store_free(*store);
		return 0;
	}
	return 1;
}

/* rename the other ixfr files */
static int ixfr_create_rename_and_delete_files(const char* zname,
	const char* zoptsname, const char* zfile, uint32_t ixfr_number,
	size_t ixfr_size, size_t cur_data_size)
{
	size_t size_in_use = cur_data_size;
	int dest_nr_files = (int)ixfr_number, maxsizehit = 0;
	int num = 1;
	while(ixfr_file_exists(zfile, num)) {
		size_t fsize = 0;
		if(!maxsizehit) {
			if(!ixfr_read_header_data_size(zoptsname, zfile, num,
				&fsize) || size_in_use + fsize > ixfr_size) {
				/* no more than this because of storage size */
				dest_nr_files = num;
				maxsizehit = 1;
			}
			size_in_use += fsize;
		}
		num++;
	}
	num--;
	/* num is now the number of ixfr files that exist */
	while(num > 0) {
		if(num+1 > dest_nr_files) {
			(void)ixfr_unlink_it(zname, zfile, num, 0);
		} else {
			if(!ixfr_rename_it(zname, zfile, num, 0, num+1, 0))
				return 0;
		}
		num--;
	}
	return 1;
}

/* finish up ixfr create processing */
static void ixfr_create_finishup(struct ixfr_create* ixfrcr,
	struct ixfr_store* store, struct zone* zone, int append_mem,
	struct nsd* nsd, const char* zfile, uint32_t ixfr_number)
{
	char log_buf[1024], nowstr[128];
	/* create the log message */
	time_t now = time(NULL);
	if(store->cancelled || ixfr_create_too_large(ixfrcr, store)) {
		/* remove unneeded files.
		 * since this ixfr cannot be created the others are useless. */
		ixfr_delete_superfluous_files(zone, zfile, 0);
		return;
	}
	snprintf(nowstr, sizeof(nowstr), "%s", ctime(&now));
	if(strchr(nowstr, '\n'))
		*strchr(nowstr, '\n') = 0;
	snprintf(log_buf, sizeof(log_buf),
		"IXFR created by NSD %s for %s %u to %u of %u bytes at time %s",
		PACKAGE_VERSION, wiredname2str(ixfrcr->zone_name),
		(unsigned)ixfrcr->old_serial, (unsigned)ixfrcr->new_serial,
		(unsigned)ixfr_data_size(store->data), nowstr);
	store->data->log_str = strdup(log_buf);
	if(!store->data->log_str) {
		log_msg(LOG_ERR, "out of memory");
		ixfr_store_free(store);
		return;
	}
	if(!ixfr_create_rename_and_delete_files(
		wiredname2str(ixfrcr->zone_name), zone->opts->name, zfile,
		ixfr_number, ixfrcr->max_size, ixfr_data_size(store->data))) {
		log_msg(LOG_ERR, "could not rename other ixfr files");
		ixfr_store_free(store);
		return;
	}
	if(!ixfr_write_file(zone, store->data, zfile, 1)) {
		log_msg(LOG_ERR, "could not write to file");
		ixfr_store_free(store);
		return;
	}
	if(append_mem) {
		ixfr_store_finish(store, nsd, log_buf);
	} else {
		ixfr_store_free(store);
	}
}

void ixfr_readup_exist(struct zone* zone, struct nsd* nsd,
	const char* zfile)
{
	/* the .ixfr file already exists with the correct serial numbers
	 * on the disk. Read up the ixfr files from the drive and put them
	 * in memory. To match the zone that has just been read.
	 * We can skip ixfr creation, and read up the files from the drive.
	 * If the files on the drive are consistent, we end up with exactly
	 * those ixfrs and that zone in memory.
	 * Presumably, the user has used nsd-checkzone to create an IXFR
	 * file and has put a new zone file, so we read up the data that
	 * we should have now.
	 * This also takes into account the config on number and size. */
	ixfr_read_from_file(nsd, zone, zfile);
}

int ixfr_create_perform(struct ixfr_create* ixfrcr, struct zone* zone,
	int append_mem, struct nsd* nsd, const char* zfile,
	uint32_t ixfr_number)
{
	struct ixfr_store store_mem, *store;
	FILE* spool;
	if(!ixfr_perform_init(ixfrcr, zone, &store_mem, &store, &spool)) {
		(void)unlink(ixfrcr->file_name);
		return 0;
	}
	if(ixfrcr->new_serial == ixfrcr->old_serial ||
		compare_serial(ixfrcr->new_serial, ixfrcr->old_serial)<0) {
		log_msg(LOG_ERR, "zone %s ixfr could not be created because the serial is the same or moves backwards, from %u to %u",
			wiredname2str(ixfrcr->zone_name),
			(unsigned)ixfrcr->old_serial,
			(unsigned)ixfrcr->new_serial);
		ixfr_store_cancel(store);
		fclose(spool);
		ixfr_store_free(store);
		(void)unlink(ixfrcr->file_name);
		ixfr_delete_superfluous_files(zone, zfile, 0);
		if(append_mem)
			ixfr_store_delixfrs(zone);
		return 0;
	}
	if(ixfr_create_already_done(ixfrcr, zone, zfile, 1)) {
		ixfr_store_cancel(store);
		fclose(spool);
		ixfr_store_free(store);
		(void)unlink(ixfrcr->file_name);
		if(append_mem) {
			ixfr_readup_exist(zone, nsd, zfile);
		}
		return 0;
	}

	if(!ixfr_create_walk_zone(spool, ixfrcr, store, zone)) {
		fclose(spool);
		ixfr_store_free(store);
		(void)unlink(ixfrcr->file_name);
		ixfr_delete_superfluous_files(zone, zfile, 0);
		return 0;
	}
	if(store->data && !store->data->oldsoa) {
		log_msg(LOG_ERR, "error spool file did not contain a SOA record");
		fclose(spool);
		ixfr_store_free(store);
		(void)unlink(ixfrcr->file_name);
		return 0;
	}
	if(!store->cancelled)
		ixfr_store_finish_data(store);
	fclose(spool);
	(void)unlink(ixfrcr->file_name);

	ixfr_create_finishup(ixfrcr, store, zone, append_mem, nsd, zfile,
		ixfr_number);
	return 1;
}

void ixfr_create_cancel(struct ixfr_create* ixfrcr)
{
	if(!ixfrcr)
		return;
	(void)unlink(ixfrcr->file_name);
	ixfr_create_free(ixfrcr);
}

int ixfr_create_from_difference(struct zone* zone, const char* zfile,
	int* ixfr_create_already_done_flag)
{
	uint32_t old_serial;
	*ixfr_create_already_done_flag = 0;
	/* only if the zone is ixfr enabled */
	if(!zone_is_ixfr_enabled(zone))
		return 0;
	/* only if ixfr create is enabled */
	if(!zone->opts->pattern->create_ixfr)
		return 0;
	/* only if there is a zone in memory to compare with */
	if(!zone->soa_rrset || !zone->apex)
		return 0;

	old_serial = zone_get_current_serial(zone);
	if(ixfr_create_already_done_serial(zone, zfile, 0, old_serial, 0)) {
		*ixfr_create_already_done_flag = 1;
		return 0;
	}

	return 1;
}
