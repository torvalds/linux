/*
 * zonec.c -- zone compiler.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <unistd.h>
#include <stdlib.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include <netinet/in.h>

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include "zonec.h"

#include "dname.h"
#include "dns.h"
#include "namedb.h"
#include "rdata.h"
#include "region-allocator.h"
#include "util.h"
#include "options.h"
#include "nsec3.h"
#include "zone.h"

/*
 * Compares two rdata arrays.
 *
 * Returns:
 *
 *	zero if they are equal
 *	non-zero if not
 *
 */
static int
zrdatacmp(uint16_t type, const union rdata_atom *rdatas, size_t rdata_count, rr_type *b)
{
	assert(rdatas);
	assert(b);

	/* One is shorter than another */
	if (rdata_count != b->rdata_count)
		return 1;

	/* Compare element by element */
	for (size_t i = 0; i < rdata_count; ++i) {
		if (rdata_atom_is_domain(type, i)) {
			if (rdata_atom_domain(rdatas[i])
			    != rdata_atom_domain(b->rdatas[i]))
			{
				return 1;
			}
		} else if(rdata_atom_is_literal_domain(type, i)) {
			if (rdata_atom_size(rdatas[i])
			    != rdata_atom_size(b->rdatas[i]))
				return 1;
			if (!dname_equal_nocase(rdata_atom_data(rdatas[i]),
				   rdata_atom_data(b->rdatas[i]),
				   rdata_atom_size(rdatas[i])))
				return 1;
		} else {
			if (rdata_atom_size(rdatas[i])
			    != rdata_atom_size(b->rdatas[i]))
			{
				return 1;
			}
			if (memcmp(rdata_atom_data(rdatas[i]),
				   rdata_atom_data(b->rdatas[i]),
				   rdata_atom_size(rdatas[i])) != 0)
			{
				return 1;
			}
		}
	}

	/* Otherwise they are equal */
	return 0;
}

/*
 * Find rrset type for any zone
 */
static rrset_type*
domain_find_rrset_any(domain_type *domain, uint16_t type)
{
	rrset_type *result = domain->rrsets;
	while (result) {
		if (rrset_rrtype(result) == type) {
			return result;
		}
		result = result->next;
	}
	return NULL;
}

/*
 * Check for DNAME type. Nothing is allowed below it
 */
static int
check_dname(zone_type* zone)
{
	domain_type* domain;
	for(domain = zone->apex; domain && domain_is_subdomain(domain,
		zone->apex); domain=domain_next(domain))
	{
		if(domain->is_existing) {
			/* there may not be DNAMEs above it */
			domain_type* parent = domain->parent;
#ifdef NSEC3
			if(domain_has_only_NSEC3(domain, NULL))
				continue;
#endif
			while(parent) {
				if(domain_find_rrset_any(parent, TYPE_DNAME)) {
					log_msg(LOG_ERR, "While checking node %s,",
						domain_to_string(domain));
					log_msg(LOG_ERR, "DNAME at %s has data below it. "
						"This is not allowed (rfc 2672).",
						domain_to_string(parent));
					return 0;
				}
				parent = parent->parent;
			}
		}
	}

	return 1;
}

static int
has_soa(domain_type* domain)
{
	rrset_type* p = NULL;
	if(!domain) return 0;
	for(p = domain->rrsets; p; p = p->next)
		if(rrset_rrtype(p) == TYPE_SOA)
			return 1;
	return 0;
}

struct zonec_state {
	struct namedb *database;
	struct domain_table *domains;
	struct region *rr_region;
	struct zone *zone;
	struct domain *domain;
	size_t errors;
	size_t records;
};

int32_t zonec_accept(
	zone_parser_t *parser,
	const zone_name_t *owner,
	uint16_t type,
	uint16_t class,
	uint32_t ttl,
	uint16_t rdlength,
	const uint8_t *rdata,
	void *user_data)
{
	struct rr *rr;
	struct rrset *rrset;
	const struct dname *dname;
	struct domain *domain;
	struct buffer buffer;
	int priority;
	union rdata_atom *rdatas;
	ssize_t rdata_count;
	struct zonec_state *state = (struct zonec_state *)user_data;

	assert(state);

	// emulate packet buffer to leverage rdata_wireformat_to_rdata_atoms
	buffer_create_from(&buffer, rdata, rdlength);

	priority = parser->options.secondary ? ZONE_WARNING : ZONE_ERROR;
	// limit to IN class
	if (class != CLASS_IN)
		zone_log(parser, priority, "only class IN is supported");

	dname = dname_make(state->rr_region, owner->octets, 1);
	assert(dname);
	domain = domain_table_insert(state->domains, dname);
	assert(domain);

	rdatas = NULL;
	rdata_count = rdata_wireformat_to_rdata_atoms(
		state->database->region, state->domains, type, rdlength, &buffer, &rdatas);
	// number of atoms must not exceed maximum of 65535 (all empty strings)
	assert(rdata_count >= 0);
	assert(rdata_count <= MAX_RDLENGTH);

	/* we have the zone already */
	if (type == TYPE_SOA) {
		if (domain != state->zone->apex) {
			char s[MAXDOMAINLEN*5];
			snprintf(s, sizeof(s), "%s", domain_to_string(domain));
			zone_log(parser, priority, "SOA record with invalid domain name, '%s' is not '%s'",
				domain_to_string(state->zone->apex), s);
		} else if (has_soa(domain)) {
			zone_log(parser, priority, "this SOA record was already encountered");
		}
		domain->is_apex = 1;
	}

	if (!domain_is_subdomain(domain, state->zone->apex)) {
		char s[MAXDOMAINLEN*5];
		snprintf(s, sizeof(s), "%s", domain_to_string(state->zone->apex));
		zone_log(parser, priority, "out of zone data: %s is outside the zone for fqdn %s",
		         s, domain_to_string(domain));
		if (!parser->options.secondary) {
			region_free_all(state->rr_region);
			return ZONE_SEMANTIC_ERROR;
		}
	}

	/* Do we have this type of rrset already? */
	rrset = domain_find_rrset(domain, state->zone, type);
	if (!rrset) {
		rrset = region_alloc(state->database->region, sizeof(*rrset));
		rrset->zone = state->zone;
		rrset->rr_count = 0;
		rrset->rrs = region_alloc(state->database->region, sizeof(*rr));

		switch (type) {
			case TYPE_CNAME:
				if (!domain_find_non_cname_rrset(domain, state->zone))
					break;
				zone_log(parser, priority, "CNAME and other data at the same name");
				break;
			case TYPE_RRSIG:
			case TYPE_NXT:
			case TYPE_SIG:
			case TYPE_NSEC:
			case TYPE_NSEC3:
				break;
			default:
				if (!domain_find_rrset(domain, state->zone, TYPE_CNAME))
					break;
				zone_log(parser, priority, "CNAME and other data at the same name");
				break;
		}

		/* Add it */
		domain_add_rrset(domain, rrset);
	} else {
		struct rr *rrs;
		if (type != TYPE_RRSIG && ttl != rrset->rrs[0].ttl) {
			zone_log(parser, ZONE_WARNING, "%s TTL %"PRIu32" does not match TTL %u of %s RRset",
				domain_to_string(domain), ttl, rrset->rrs[0].ttl,
					rrtype_to_string(type));
		}

		/* Search for possible duplicates... */
		for (int i = 0; i < rrset->rr_count; i++) {
			if (zrdatacmp(type, rdatas, rdata_count, &rrset->rrs[i]) != 0)
				continue;
			/* Discard the duplicates... */
			for (size_t j = 0; j < (size_t)rdata_count; j++) {
				size_t size;
				if (rdata_atom_is_domain(type, j))
					continue;
				size = rdata_atom_size(rdatas[j]) + sizeof(uint16_t);
				region_recycle(state->database->region, rdatas[j].data, size);
			}
			region_recycle(state->database->region, rdatas, sizeof(*rdatas) * rdata_count);
			region_free_all(state->rr_region);
			return 0;
		}

		switch (type) {
			case TYPE_CNAME:
				zone_log(parser, priority, "multiple CNAMEs at the same name");
				break;
			case TYPE_DNAME:
				zone_log(parser, priority, "multiple DNAMEs at the same name");
				break;
			default:
				break;
		}

		/* Add it... */
		rrs = rrset->rrs;
		rrset->rrs = region_alloc_array(state->database->region, rrset->rr_count + 1, sizeof(*rr));
		memcpy(rrset->rrs, rrs, rrset->rr_count * sizeof(*rr));
		region_recycle(state->database->region, rrs, rrset->rr_count * sizeof(*rr));
	}

	rr = &rrset->rrs[rrset->rr_count++];
	rr->owner = domain;
	rr->rdatas = rdatas;
	rr->ttl = ttl;
	rr->type = type;
	rr->klass = class;
	rr->rdata_count = rdata_count;

	/* Check we have SOA */
	if (rr->owner == state->zone->apex)
		apex_rrset_checks(state->database, rrset, rr->owner);

	state->records++;
	region_free_all(state->rr_region);
	return 0;
}

static int32_t zonec_include(
  zone_parser_t *parser,
  const char *file,
  const char *path,
  void *user_data)
{
	char **paths;
	struct zonec_state *state;
	struct namedb *database;
	struct zone *zone;

	(void)parser;
	(void)file;

	state = (struct zonec_state *)user_data;
	database = state->database;
	zone = state->zone;

	assert((zone->includes.count == 0) == (zone->includes.paths == NULL));

	for (size_t i=0; i < zone->includes.count; i++)
		if (strcmp(path, zone->includes.paths[i]) == 0)
			return 0;

	paths = region_alloc_array(
		database->region, zone->includes.count + 1, sizeof(*paths));
	if (zone->includes.count) {
		const size_t size = zone->includes.count * sizeof(*paths);
		memcpy(paths, zone->includes.paths, size);
		region_recycle(database->region, zone->includes.paths, size);
	}
	paths[zone->includes.count] = region_strdup(database->region, path);
	zone->includes.count++;
	zone->includes.paths = paths;
	return 0;
}

static void zonec_log(
	zone_parser_t *parser,
	uint32_t category,
	const char *file,
	size_t line,
	const char *message,
	void *user_data)
{
	int priority;
	struct zonec_state *state = (struct zonec_state *)user_data;

	assert(state);
	(void)parser;

	switch (category) {
	case ZONE_INFO:
		priority = LOG_INFO;
		break;
	case ZONE_WARNING:
		priority = LOG_WARNING;
		break;
	default:
		priority = LOG_ERR;
		state->errors++;
		break;
	}

	if (file)
		log_msg(priority, "%s:%zu: %s", file, line, message);
	else
		log_msg(priority, "%s", message);
}

/*
 * Reads the specified zone into the memory
 * nsd_options can be NULL if no config file is passed.
 */
unsigned int
zonec_read(
	struct namedb *database,
	struct domain_table *domains,
	const char *name,
	const char *zonefile,
	struct zone *zone)
{
	const struct dname *origin;
	zone_parser_t parser;
	zone_options_t options;
	zone_name_buffer_t name_buffer;
	zone_rdata_buffer_t rdata_buffer;
	struct zonec_state state;
	zone_buffers_t buffers = { 1, &name_buffer, &rdata_buffer };

	state.database = database;
	state.domains = domains;
	state.rr_region = region_create(xalloc, free);
	state.zone = zone;
	state.domain = NULL;
	state.errors = 0;
	state.records = 0;

	origin = domain_dname(zone->apex);
	memset(&options, 0, sizeof(options));
	options.origin.octets = dname_name(origin);
	options.origin.length = origin->name_size;
	options.default_ttl = DEFAULT_TTL;
	options.default_class = CLASS_IN;
	options.secondary = zone_is_slave(zone->opts) != 0;
	options.pretty_ttls = true; /* non-standard, for backwards compatibility */
	options.log.callback = &zonec_log;
	options.accept.callback = &zonec_accept;
	options.include.callback = &zonec_include;

	/* Parse and process all RRs.  */
	if (zone_parse(&parser, &options, &buffers, zonefile, &state) != 0) {
		region_destroy(state.rr_region);
		return state.errors;
	}

	/* Check if zone file contained a correct SOA record */
	if (!zone) {
		log_msg(LOG_ERR, "zone configured as '%s' has no content.", name);
		state.errors++;
	} else if (!zone->soa_rrset || zone->soa_rrset->rr_count == 0) {
		log_msg(LOG_ERR, "zone configured as '%s' has no SOA record", name);
		state.errors++;
	} else if (dname_compare(domain_dname(zone->soa_rrset->rrs[0].owner), origin) != 0) {
		log_msg(LOG_ERR, "zone configured as '%s', but SOA has owner '%s'",
		        name, domain_to_string(zone->soa_rrset->rrs[0].owner));
		state.errors++;
	}

	if(!zone_is_slave(zone->opts) && !check_dname(zone))
		state.errors++;

	region_destroy(state.rr_region);
	return state.errors;
}

void
apex_rrset_checks(namedb_type* db, rrset_type* rrset, domain_type* domain)
{
	uint32_t soa_minimum;
	unsigned i;
	zone_type* zone = rrset->zone;
	assert(domain == zone->apex);
	(void)domain;
	if (rrset_rrtype(rrset) == TYPE_SOA) {
		zone->soa_rrset = rrset;

		/* BUG #103 add another soa with a tweaked ttl */
		if(zone->soa_nx_rrset == 0) {
			zone->soa_nx_rrset = region_alloc(db->region,
				sizeof(rrset_type));
			zone->soa_nx_rrset->rr_count = 1;
			zone->soa_nx_rrset->next = 0;
			zone->soa_nx_rrset->zone = zone;
			zone->soa_nx_rrset->rrs = region_alloc(db->region,
				sizeof(rr_type));
		}
		memcpy(zone->soa_nx_rrset->rrs, rrset->rrs, sizeof(rr_type));

		/* check the ttl and MINIMUM value and set accordingly */
		memcpy(&soa_minimum, rdata_atom_data(rrset->rrs->rdatas[6]),
				rdata_atom_size(rrset->rrs->rdatas[6]));
		if (rrset->rrs->ttl > ntohl(soa_minimum)) {
			zone->soa_nx_rrset->rrs[0].ttl = ntohl(soa_minimum);
		}
	} else if (rrset_rrtype(rrset) == TYPE_NS) {
		zone->ns_rrset = rrset;
	} else if (rrset_rrtype(rrset) == TYPE_RRSIG) {
		for (i = 0; i < rrset->rr_count; ++i) {
			if(rr_rrsig_type_covered(&rrset->rrs[i])==TYPE_DNSKEY){
				zone->is_secure = 1;
				break;
			}
		}
	}
}
