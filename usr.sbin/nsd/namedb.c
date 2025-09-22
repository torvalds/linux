/*
 * namedb.c -- common namedb operations.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "namedb.h"
#include "nsec3.h"

static domain_type *
allocate_domain_info(domain_table_type* table,
		     const dname_type* dname,
		     domain_type* parent)
{
	domain_type *result;

	assert(table);
	assert(dname);
	assert(parent);

	result = (domain_type *) region_alloc(table->region,
					      sizeof(domain_type));
#ifdef USE_RADIX_TREE
	result->dname 
#else
	result->node.key
#endif
		= dname_partial_copy(
		table->region, dname, domain_dname(parent)->label_count + 1);
	result->parent = parent;
	result->wildcard_child_closest_match = result;
	result->rrsets = NULL;
	result->usage = 0;
#ifdef NSEC3
	result->nsec3 = NULL;
#endif
	result->is_existing = 0;
	result->is_apex = 0;
	assert(table->numlist_last); /* it exists because root exists */
	/* push this domain at the end of the numlist */
	result->number = table->numlist_last->number+1;
	result->numlist_next = NULL;
	result->numlist_prev = table->numlist_last;
	table->numlist_last->numlist_next = result;
	table->numlist_last = result;

	return result;
}

#ifdef NSEC3
void
allocate_domain_nsec3(domain_table_type* table, domain_type* result)
{
	if(result->nsec3)
		return;
	result->nsec3 = (struct nsec3_domain_data*) region_alloc(table->region,
		sizeof(struct nsec3_domain_data));
	result->nsec3->nsec3_cover = NULL;
	result->nsec3->nsec3_wcard_child_cover = NULL;
	result->nsec3->nsec3_ds_parent_cover = NULL;
	result->nsec3->nsec3_is_exact = 0;
	result->nsec3->nsec3_ds_parent_is_exact = 0;
	result->nsec3->hash_wc = NULL;
	result->nsec3->ds_parent_hash = NULL;
	result->nsec3->prehash_prev = NULL;
	result->nsec3->prehash_next = NULL;
	result->nsec3->nsec3_node.key = NULL;
}
#endif /* NSEC3 */

void
numlist_make_last(domain_table_type* table, domain_type* domain)
{
	uint32_t sw;
	domain_type* last = table->numlist_last;
	if(domain == last)
		return;
	/* swap numbers with the last element */
	sw = domain->number;
	domain->number = last->number;
	last->number = sw;
	/* swap list position with the last element */
	assert(domain->numlist_next);
	assert(last->numlist_prev);
	if(domain->numlist_next != last) {
		/* case 1: there are nodes between domain .. last */
		domain_type* span_start = domain->numlist_next;
		domain_type* span_end = last->numlist_prev;
		/* these assignments walk the new list from start to end */
		if(domain->numlist_prev)
			domain->numlist_prev->numlist_next = last;
		last->numlist_prev = domain->numlist_prev;
		last->numlist_next = span_start;
		span_start->numlist_prev = last;
		span_end->numlist_next = domain;
		domain->numlist_prev = span_end;
		domain->numlist_next = NULL;
	} else {
		/* case 2: domain and last are neighbors */
		/* these assignments walk the new list from start to end */
		if(domain->numlist_prev)
			domain->numlist_prev->numlist_next = last;
		last->numlist_prev = domain->numlist_prev;
		last->numlist_next = domain;
		domain->numlist_prev = last;
		domain->numlist_next = NULL;
	}
	table->numlist_last = domain;
}

domain_type*
numlist_pop_last(domain_table_type* table)
{
	domain_type* d = table->numlist_last;
	table->numlist_last = table->numlist_last->numlist_prev;
	if(table->numlist_last)
		table->numlist_last->numlist_next = NULL;
	return d;
}

/** see if a domain is eligible to be deleted, and thus is not used */
static int
domain_can_be_deleted(domain_type* domain)
{
	domain_type* n;
	/* it has data or it has usage, do not delete it */
	if(domain->rrsets) return 0;
	if(domain->usage) return 0;
	n = domain_next(domain);
	/* it has children domains, do not delete it */
	if(n && domain_is_subdomain(n, domain))
		return 0;
	return 1;
}

#ifdef NSEC3
/** see if domain is on the prehash list */
int domain_is_prehash(domain_table_type* table, domain_type* domain)
{
	if(domain->nsec3
		&& (domain->nsec3->prehash_prev || domain->nsec3->prehash_next))
		return 1;
	return (table->prehash_list == domain);
}

/** remove domain node from NSEC3 tree in hash space */
void
zone_del_domain_in_hash_tree(rbtree_type* tree, rbnode_type* node)
{
	if(!node->key)
		return;
	rbtree_delete(tree, node->key);
	/* note that domain is no longer in the tree */
	node->key = NULL;
}

/** clear the prehash list */
void prehash_clear(domain_table_type* table)
{
	domain_type* d = table->prehash_list, *n;
	while(d) {
		n = d->nsec3->prehash_next;
		d->nsec3->prehash_prev = NULL;
		d->nsec3->prehash_next = NULL;
		d = n;
	}
	table->prehash_list = NULL;
}

/** add domain to prehash list */
void
prehash_add(domain_table_type* table, domain_type* domain)
{
	if(domain_is_prehash(table, domain))
		return;
	allocate_domain_nsec3(table, domain);
	domain->nsec3->prehash_next = table->prehash_list;
	if(table->prehash_list)
		table->prehash_list->nsec3->prehash_prev = domain;
	table->prehash_list = domain;
}

/** remove domain from prehash list */
void
prehash_del(domain_table_type* table, domain_type* domain)
{
	if(domain->nsec3->prehash_next)
		domain->nsec3->prehash_next->nsec3->prehash_prev =
			domain->nsec3->prehash_prev;
	if(domain->nsec3->prehash_prev)
		domain->nsec3->prehash_prev->nsec3->prehash_next =
			domain->nsec3->prehash_next;
	else	table->prehash_list = domain->nsec3->prehash_next;
	domain->nsec3->prehash_next = NULL;
	domain->nsec3->prehash_prev = NULL;
}
#endif /* NSEC3 */

/** perform domain name deletion */
static void
do_deldomain(namedb_type* db, domain_type* domain)
{
	assert(domain && domain->parent); /* exists and not root */
	/* first adjust the number list so that domain is the last one */
	numlist_make_last(db->domains, domain);
	/* pop off the domain from the number list */
	(void)numlist_pop_last(db->domains);

#ifdef NSEC3
	/* if on prehash list, remove from prehash */
	if(domain_is_prehash(db->domains, domain))
		prehash_del(db->domains, domain);

	/* see if nsec3-nodes are used */
	if(domain->nsec3) {
		if(domain->nsec3->nsec3_node.key)
			zone_del_domain_in_hash_tree(nsec3_tree_zone(db, domain)
				->nsec3tree, &domain->nsec3->nsec3_node);
		if(domain->nsec3->hash_wc) {
			if(domain->nsec3->hash_wc->hash.node.key)
				zone_del_domain_in_hash_tree(nsec3_tree_zone(db, domain)
					->hashtree, &domain->nsec3->hash_wc->hash.node);
			if(domain->nsec3->hash_wc->wc.node.key)
				zone_del_domain_in_hash_tree(nsec3_tree_zone(db, domain)
					->wchashtree, &domain->nsec3->hash_wc->wc.node);
		}
		if(domain->nsec3->ds_parent_hash && domain->nsec3->ds_parent_hash->node.key)
			zone_del_domain_in_hash_tree(nsec3_tree_dszone(db, domain)
				->dshashtree, &domain->nsec3->ds_parent_hash->node);
		if(domain->nsec3->hash_wc) {
			region_recycle(db->domains->region,
				domain->nsec3->hash_wc,
				sizeof(nsec3_hash_wc_node_type));
		}
		if(domain->nsec3->ds_parent_hash) {
			region_recycle(db->domains->region,
				domain->nsec3->ds_parent_hash,
				sizeof(nsec3_hash_node_type));
		}
		region_recycle(db->domains->region, domain->nsec3,
			sizeof(struct nsec3_domain_data));
	}
#endif /* NSEC3 */

	/* see if this domain is someones wildcard-child-closest-match,
	 * which can only be the parent, and then it should use the
	 * one-smaller than this domain as closest-match. */
	if(domain->parent->wildcard_child_closest_match == domain)
		domain->parent->wildcard_child_closest_match =
			domain_previous_existing_child(domain);

	/* actual removal */
#ifdef USE_RADIX_TREE
	radix_delete(db->domains->nametree, domain->rnode);
#else
	rbtree_delete(db->domains->names_to_domains, domain->node.key);
#endif
	region_recycle(db->domains->region, domain_dname(domain),
		dname_total_size(domain_dname(domain)));
	region_recycle(db->domains->region, domain, sizeof(domain_type));
}

void
domain_table_deldomain(namedb_type* db, domain_type* domain)
{
	domain_type* parent;

	while(domain_can_be_deleted(domain)) {
		parent = domain->parent;
		/* delete it */
		do_deldomain(db, domain);
		/* test parent */
		domain = parent;
	}
}

void hash_tree_delete(region_type* region, rbtree_type* tree)
{
	region_recycle(region, tree, sizeof(rbtree_type));
}

/** add domain nsec3 node to hashedspace tree */
void zone_add_domain_in_hash_tree(region_type* region, rbtree_type** tree,
	int (*cmpf)(const void*, const void*),
	domain_type* domain, rbnode_type* node)
{
	if(!*tree)
		*tree = rbtree_create(region, cmpf);
	if(node->key && node->key == domain
	&& rbtree_search(*tree, domain) == node)
		return;
	memset(node, 0, sizeof(rbnode_type));
	node->key = domain;
	rbtree_insert(*tree, node);
}

domain_table_type *
domain_table_create(region_type* region)
{
	const dname_type* origin;
	domain_table_type* result;
	domain_type* root;

	assert(region);

	origin = dname_make(region, (uint8_t *) "", 0);

	root = (domain_type *) region_alloc(region, sizeof(domain_type));
#ifdef USE_RADIX_TREE
	root->dname
#else
	root->node.key
#endif
		= origin;
	root->parent = NULL;
	root->wildcard_child_closest_match = root;
	root->rrsets = NULL;
	root->number = 1; /* 0 is used for after header */
	root->usage = 1; /* do not delete root, ever */
	root->is_existing = 0;
	root->is_apex = 0;
	root->numlist_prev = NULL;
	root->numlist_next = NULL;
#ifdef NSEC3
	root->nsec3 = NULL;
#endif

	result = (domain_table_type *) region_alloc(region,
						    sizeof(domain_table_type));
	result->region = region;
#ifdef USE_RADIX_TREE
	result->nametree = radix_tree_create(region);
	root->rnode = radname_insert(result->nametree, dname_name(root->dname),
		root->dname->name_size, root);
#else
	result->names_to_domains = rbtree_create(
		region, (int (*)(const void *, const void *)) dname_compare);
	rbtree_insert(result->names_to_domains, (rbnode_type *) root);
#endif

	result->root = root;
	result->numlist_last = root;
#ifdef NSEC3
	result->prehash_list = NULL;
#endif

	return result;
}

int
domain_table_search(domain_table_type *table,
		   const dname_type   *dname,
		   domain_type       **closest_match,
		   domain_type       **closest_encloser)
{
	int exact;
	uint8_t label_match_count;

	assert(table);
	assert(dname);
	assert(closest_match);
	assert(closest_encloser);

#ifdef USE_RADIX_TREE
	exact = radname_find_less_equal(table->nametree, dname_name(dname),
		dname->name_size, (struct radnode**)closest_match);
	*closest_match = (domain_type*)((*(struct radnode**)closest_match)->elem);
#else
	exact = rbtree_find_less_equal(table->names_to_domains, dname, (rbnode_type **) closest_match);
#endif
	assert(*closest_match);

	*closest_encloser = *closest_match;

	if (!exact) {
		label_match_count = dname_label_match_count(
			domain_dname(*closest_encloser),
			dname);
		assert(label_match_count < dname->label_count);
		while (label_match_count < domain_dname(*closest_encloser)->label_count) {
			(*closest_encloser) = (*closest_encloser)->parent;
			assert(*closest_encloser);
		}
	}

	return exact;
}

domain_type *
domain_table_find(domain_table_type* table,
		  const dname_type* dname)
{
	domain_type* closest_match;
	domain_type* closest_encloser;
	int exact;

	exact = domain_table_search(
		table, dname, &closest_match, &closest_encloser);
	return exact ? closest_encloser : NULL;
}


domain_type *
domain_table_insert(domain_table_type* table,
		    const dname_type* dname)
{
	domain_type* closest_match;
	domain_type* closest_encloser;
	domain_type* result;
	int exact;

	assert(table);
	assert(dname);

	exact = domain_table_search(
		table, dname, &closest_match, &closest_encloser);
	if (exact) {
		result = closest_encloser;
	} else {
		assert(domain_dname(closest_encloser)->label_count < dname->label_count);

		/* Insert new node(s).  */
		do {
			result = allocate_domain_info(table,
						      dname,
						      closest_encloser);
#ifdef USE_RADIX_TREE
			result->rnode = radname_insert(table->nametree,
				dname_name(result->dname),
				result->dname->name_size, result);
#else
			rbtree_insert(table->names_to_domains, (rbnode_type *) result);
#endif

			/*
			 * If the newly added domain name is larger
			 * than the parent's current
			 * wildcard_child_closest_match but smaller or
			 * equal to the wildcard domain name, update
			 * the parent's wildcard_child_closest_match
			 * field.
			 */
			if (label_compare(dname_name(domain_dname(result)),
					  (const uint8_t *) "\001*") <= 0
			    && dname_compare(domain_dname(result),
					     domain_dname(closest_encloser->wildcard_child_closest_match)) > 0)
			{
				closest_encloser->wildcard_child_closest_match
					= result;
			}
			closest_encloser = result;
		} while (domain_dname(closest_encloser)->label_count < dname->label_count);
	}

	return result;
}

domain_type *domain_previous_existing_child(domain_type* domain)
{
	domain_type* parent = domain->parent;
	domain = domain_previous(domain);
	while(domain && !domain->is_existing) {
		if(domain == parent) /* do not walk back above parent */
			return parent;
		domain = domain_previous(domain);
	}
	return domain;
}

void
domain_add_rrset(domain_type* domain, rrset_type* rrset)
{
#if 0 	/* fast */
	rrset->next = domain->rrsets;
	domain->rrsets = rrset;
#else
	/* preserve ordering, add at end */
	rrset_type** p = &domain->rrsets;
	while(*p)
		p = &((*p)->next);
	*p = rrset;
	rrset->next = 0;
#endif

	while (domain && !domain->is_existing) {
		domain->is_existing = 1;
		/* does this name in existance update the parent's
		 * wildcard closest match? */
		if(domain->parent
		   && label_compare(dname_name(domain_dname(domain)),
			(const uint8_t *) "\001*") <= 0
		   && dname_compare(domain_dname(domain),
		   	domain_dname(domain->parent->wildcard_child_closest_match)) > 0) {
			domain->parent->wildcard_child_closest_match = domain;
		}
		domain = domain->parent;
	}
}


rrset_type *
domain_find_rrset(domain_type* domain, zone_type* zone, uint16_t type)
{
	rrset_type* result = domain->rrsets;

	while (result) {
		if (result->zone == zone && rrset_rrtype(result) == type) {
			return result;
		}
		result = result->next;
	}
	return NULL;
}

rrset_type *
domain_find_any_rrset(domain_type* domain, zone_type* zone)
{
	rrset_type* result = domain->rrsets;

	while (result) {
		if (result->zone == zone) {
			return result;
		}
		result = result->next;
	}
	return NULL;
}

zone_type *
domain_find_zone(namedb_type* db, domain_type* domain)
{
	rrset_type* rrset;
	while (domain) {
		if(domain->is_apex) {
			for (rrset = domain->rrsets; rrset; rrset = rrset->next) {
				if (rrset_rrtype(rrset) == TYPE_SOA) {
					return rrset->zone;
				}
			}
			return namedb_find_zone(db, domain_dname(domain));
		}
		domain = domain->parent;
	}
	return NULL;
}

zone_type *
domain_find_parent_zone(namedb_type* db, zone_type* zone)
{
	rrset_type* rrset;

	assert(zone);

	for (rrset = zone->apex->rrsets; rrset; rrset = rrset->next) {
		if (rrset->zone != zone && rrset_rrtype(rrset) == TYPE_NS) {
			return rrset->zone;
		}
	}
	/* the NS record in the parent zone above this zone is not present,
	 * workaround to find that parent zone anyway */
	if(zone->apex->parent)
		return domain_find_zone(db, zone->apex->parent);
	return NULL;
}

domain_type *
domain_find_ns_rrsets(domain_type* domain, zone_type* zone, rrset_type **ns)
{
	/* return highest NS RRset in the zone that is a delegation above */
	domain_type* result = NULL;
	rrset_type* rrset = NULL;
	while (domain && domain != zone->apex) {
		rrset = domain_find_rrset(domain, zone, TYPE_NS);
		if (rrset) {
			*ns = rrset;
			result = domain;
		}
		domain = domain->parent;
	}

	if(result)
		return result;

	*ns = NULL;
	return NULL;
}

domain_type *
find_dname_above(domain_type* domain, zone_type* zone)
{
	domain_type* d = domain->parent;
	while(d && d != zone->apex) {
		if(domain_find_rrset(d, zone, TYPE_DNAME))
			return d;
		d = d->parent;
	}
	return NULL;
}

int
domain_is_glue(domain_type* domain, zone_type* zone)
{
	rrset_type* unused;
	domain_type* ns_domain = domain_find_ns_rrsets(domain, zone, &unused);
	return (ns_domain != NULL &&
		domain_find_rrset(ns_domain, zone, TYPE_SOA) == NULL);
}

domain_type *
domain_wildcard_child(domain_type* domain)
{
	domain_type* wildcard_child;

	assert(domain);
	assert(domain->wildcard_child_closest_match);

	wildcard_child = domain->wildcard_child_closest_match;
	if (wildcard_child != domain
	    && label_is_wildcard(dname_name(domain_dname(wildcard_child))))
	{
		return wildcard_child;
	} else {
		return NULL;
	}
}

int
zone_is_secure(zone_type* zone)
{
	assert(zone);
	return zone->is_secure;
}

uint16_t
rr_rrsig_type_covered(rr_type* rr)
{
	assert(rr->type == TYPE_RRSIG);
	assert(rr->rdata_count > 0);
	assert(rdata_atom_size(rr->rdatas[0]) == sizeof(uint16_t));

	return ntohs(* (uint16_t *) rdata_atom_data(rr->rdatas[0]));
}

zone_type *
namedb_find_zone(namedb_type* db, const dname_type* dname)
{
	struct radnode* n = radname_search(db->zonetree, dname_name(dname),
		dname->name_size);
	if(n) return (zone_type*)n->elem;
	return NULL;
}

rrset_type *
domain_find_non_cname_rrset(domain_type* domain, zone_type* zone)
{
	/* find any rrset type that is not allowed next to a CNAME */
	/* nothing is allowed next to a CNAME, except RRSIG, NSEC, NSEC3 */
	rrset_type *result = domain->rrsets;

	while (result) {
		if (result->zone == zone && /* here is the list of exceptions*/
			rrset_rrtype(result) != TYPE_CNAME &&
			rrset_rrtype(result) != TYPE_RRSIG &&
			rrset_rrtype(result) != TYPE_NXT &&
			rrset_rrtype(result) != TYPE_SIG &&
			rrset_rrtype(result) != TYPE_NSEC &&
			rrset_rrtype(result) != TYPE_NSEC3 ) {
			return result;
		}
		result = result->next;
	}
	return NULL;
}

int
namedb_lookup(struct namedb* db,
	      const dname_type* dname,
	      domain_type     **closest_match,
	      domain_type     **closest_encloser)
{
	return domain_table_search(
		db->domains, dname, closest_match, closest_encloser);
}

void zone_rr_iter_init(struct zone_rr_iter *iter, struct zone *zone)
{
	assert(iter != NULL);
	assert(zone != NULL);
	memset(iter, 0, sizeof(*iter));
	iter->zone = zone;
}

rr_type *zone_rr_iter_next(struct zone_rr_iter *iter)
{
	assert(iter != NULL);
	assert(iter->zone != NULL);

	if(iter->index == -1) {
		assert(iter->domain == NULL);
		assert(iter->rrset == NULL);
		return NULL;
	} else if(iter->rrset == NULL) {
		/* ensure SOA RR is returned first */
		assert(iter->domain == NULL);
		assert(iter->index == 0);
		iter->rrset = iter->zone->soa_rrset;
	}

	while(iter->rrset != NULL) {
		if(iter->index < iter->rrset->rr_count) {
			return &iter->rrset->rrs[iter->index++];
		}
		iter->index = 0;
		if(iter->domain == NULL) {
			assert(iter->rrset == iter->zone->soa_rrset);
			iter->domain = iter->zone->apex;
			iter->rrset = iter->domain->rrsets;
		} else {
			iter->rrset = iter->rrset->next;
		}
		/* ensure SOA RR is not returned again and RR belongs to zone */
		while((iter->rrset == NULL && iter->domain != NULL) ||
		      (iter->rrset != NULL && (iter->rrset == iter->zone->soa_rrset ||
		                               iter->rrset->zone != iter->zone)))
		{
			if(iter->rrset != NULL) {
				iter->rrset = iter->rrset->next;
			} else {
				iter->domain = domain_next(iter->domain);
				if(iter->domain != NULL &&
				   dname_is_subdomain(domain_dname(iter->domain),
				                      domain_dname(iter->zone->apex)))
				{
					iter->rrset = iter->domain->rrsets;
				}
			}
		}
	}

	assert(iter->rrset == NULL);
	assert(iter->domain == NULL);
	iter->index = -1;

	return NULL;
}
