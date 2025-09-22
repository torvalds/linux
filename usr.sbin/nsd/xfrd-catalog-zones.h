/*
 * xfrd-catalog-zones.h -- catalog zone implementation for NSD
 *
 * Copyright (c) 2024, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 */
#ifndef XFRD_CATALOG_ZONES_H
#define XFRD_CATALOG_ZONES_H
#include "xfrd.h"
struct xfrd_producer_member;
struct xfrd_producer_xfr;

/**
 * Catalog zones withing the xfrd context
 */
struct xfrd_catalog_consumer_zone {
	/* For indexing in struc xfrd_state { rbtree_type* catalog_consumer_zones; } */
	rbnode_type node;

	/* Associated zone options with this catalog consumer zone */
	struct zone_options* options;

	/* Member zones indexed by member_id */
	rbtree_type member_ids;

	/* Last time processed, compare with zone->mtime to see if we need to process */
	struct timespec mtime;

	/* The reason for this zone to be invalid, or NULL if it is valid */
	char *invalid;
} ATTR_PACKED;

/**
 * Catalog producer zones withing the xfrd context
 */
struct xfrd_catalog_producer_zone {
	/* For indexing in struc xfrd_state { rbtree_type* catalog_producer_zones; } */
	rbnode_type node;

	/* Associated zone options with this catalog consumer zone */
	struct zone_options* options;

	/* Member zones indexed by member_id */
	rbtree_type member_ids;

	/* SOA serial for this zone */
	uint32_t serial;

	/* Stack of members to delete from this catalog producer zone */
	struct xfrd_producer_member* to_delete;

	/* Stack of member zones to add to this catalog producer zone */
	struct xfrd_producer_member* to_add;

	/* To cleanup on disk xfr files */
	struct xfrd_producer_xfr* latest_pxfr;

	/* Set if next generated xfr for the producer zone should be axfr */
	unsigned axfr: 1;
} ATTR_PACKED;

/**
 * Data to add or remove from a catalog producer zone
 */
struct xfrd_producer_member {
	const dname_type* member_id;
	const dname_type* member_zone_name;
	const char* group_name;
	struct xfrd_producer_member* next;
} ATTR_PACKED;

/**
 * To track applied generated transfers from catalog producer zones
 */
struct xfrd_producer_xfr {
	uint32_t serial;
	uint64_t xfrfilenumber;
	struct xfrd_producer_xfr** prev_next_ptr;
	struct xfrd_producer_xfr*  next;
} ATTR_PACKED;

/* Initialize as a catalog consumer zone */
void xfrd_init_catalog_consumer_zone(xfrd_state_type* xfrd,
		struct zone_options* zone);

/* To be called if and a zone is no longer a catalog zone (changed pattern) */
void xfrd_deinit_catalog_consumer_zone(xfrd_state_type* xfrd,
		const dname_type* dname);

/* make the catalog consumer zone invalid for given reason */
void make_catalog_consumer_invalid(
		struct xfrd_catalog_consumer_zone *consumer_zone,
		const char *format, ...) ATTR_FORMAT(printf, 2, 3);

/* Return the reason a zone is invalid, or NULL on a valid catalog */
const char *invalid_catalog_consumer_zone(struct zone_options* zone);

/* make the catalog consumer zone valid again */
void make_catalog_consumer_valid(
		struct xfrd_catalog_consumer_zone *consumer_zone);

/* Check the catalog consumer zone files (or file if zone is given) */
void xfrd_check_catalog_consumer_zonefiles(const dname_type* name);

/* process the catalog consumer zones, load if needed */
void xfrd_process_catalog_consumer_zones();


/* Add (or change) <member_id> PTR <member_zone_name>, and 
 * group.<member_id> TXT <pattern->pname> to the associated producer zone by 
 * constructed xfr. make cmz->member_id if needed. */
void xfrd_add_catalog_producer_member(struct catalog_member_zone* cmz);

/* Delete <member_id> PTR <member_zone_name>, and 
 * group.<member_id> TXT <pattern->pname> from the associated producer zone by
 * constructed xfr. Return 1 if zone is deleted. In this case, member_zone_name
 * is taken over by xfrd and cannot be recycled by the caller. member_zone_name
 * must have been allocated int the xfrd->nsd->options->region
 */
int xfrd_del_catalog_producer_member(xfrd_state_type* xfrd,
		const dname_type* dname);

/* process the catalog producer zones */
void xfrd_process_catalog_producer_zones();

#endif

