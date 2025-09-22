/*
 * nsec3.h -- nsec3 handling.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
#ifndef NSEC3_H
#define NSEC3_H

#ifdef NSEC3
struct udb_ptr;
struct domain;
struct dname;
struct region;
struct zone;
struct namedb;
struct query;
struct answer;
struct rr;

/*
 * calculate prehash information for zone.
 */
void prehash_zone(struct namedb* db, struct zone* zone);
/*
 * calculate prehash for zone, assumes no partial precompile or prehashlist
 */
void prehash_zone_complete(struct namedb* db, struct zone* zone);

/*
 * finds nsec3 that covers the given domain hash.
 * returns true if the find is exact.
 */
int nsec3_find_cover(struct zone* zone, uint8_t* hash, size_t hashlen,
	struct domain** result);

/*
 * _answer_ Routines used to add the correct nsec3 record to a query answer.
 * cnames etc may have been followed, hence original name.
 */
/*
 * add proof for wildcards that the name below the wildcard.parent
 * does not exist
 */
void nsec3_answer_wildcard(struct query* query, struct answer* answer,
        struct domain* wildcard, const struct dname* qname);

/*
 * add NSEC3 to provide domain name but not rrset exists,
 * this could be a query for a DS or NSEC3 type
 */
void nsec3_answer_nodata(struct query *query, struct answer *answer,
	struct domain *original);

/*
 * add NSEC3 for a delegation (optout stuff)
 */
void nsec3_answer_delegation(struct query *query, struct answer *answer);

/*
 * add NSEC3 for authoritative answers.
 * match==0 is an nxdomain.
 */
void nsec3_answer_authoritative(struct domain** match, struct query *query,
	struct answer *answer, struct domain* closest_encloser,
	const struct dname* qname);

/*
 * True if domain is a NSEC3 (+RRSIG) data only variety.
 * pass nonNULL zone to filter for particular zone.
 */
int domain_has_only_NSEC3(struct domain* domain, struct zone* zone);

/* get hashed bytes */
void nsec3_hash_and_store(struct zone* zone, const struct dname* dname,
	uint8_t* store);
/* see if NSEC3 record uses the params in use for the zone */
int nsec3_rr_uses_params(struct rr* rr, struct zone* zone);
/* number of NSEC3s that are in the zone chain */
int nsec3_in_chain_count(struct domain* domain, struct zone* zone);
/* find previous NSEC3, or, lastinzone, or, NULL */
struct domain* nsec3_chain_find_prev(struct zone* zone, struct domain* domain);
/* clear nsec3 precompile for the zone */
void nsec3_clear_precompile(struct namedb* db, struct zone* zone);
/* if domain is part of nsec3hashed domains of a zone */
int nsec3_domain_part_of_zone(struct domain* d, struct zone* z);
/* condition when a domain is precompiled */
int nsec3_condition_hash(struct domain* d, struct zone* z);
/* condition when a domain is ds precompiled */
int nsec3_condition_dshash(struct domain* d, struct zone* z);
/* set nsec3param for this zone or NULL if no NSEC3 available */
void nsec3_find_zone_param(struct namedb* db, struct zone* zone,
	struct rr* avoid_rr, int checkchain);
/* hash domain and wcchild, and lookup nsec3 in tree, and precompile */
void nsec3_precompile_domain(struct namedb* db, struct domain* domain,
	struct zone* zone, struct region* tmpregion);
/* hash ds_parent_cover, and lookup nsec3 and precompile */
void nsec3_precompile_domain_ds(struct namedb* db, struct domain* domain,
	struct zone* zone);
/* put nsec3 into nsec3tree and adjust zonelast */
void nsec3_precompile_nsec3rr(struct namedb* db, struct domain* domain,
	struct zone* zone);
/* precompile entire zone, assumes all is null at start */
void nsec3_precompile_newparam(struct namedb* db, struct zone* zone);
/* create b32.zone for a hash, allocated in the region */
const struct dname* nsec3_b32_create(struct region* region, struct zone* zone,
	unsigned char* hash);
/* create trees for nsec3 updates and lookups in zone */
void nsec3_zone_trees_create(struct region* region, struct zone* zone);
/* lookup zone that contains domain's nsec3 trees */
struct zone* nsec3_tree_zone(struct namedb* db, struct domain* domain);
/* lookup zone that contains domain's ds tree */
struct zone* nsec3_tree_dszone(struct namedb* db, struct domain* domain);

#endif /* NSEC3 */
#endif /* NSEC3_H*/
