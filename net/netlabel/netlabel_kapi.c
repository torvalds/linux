// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NetLabel Kernel API
 *
 * This file defines the kernel API for the NetLabel system.  The NetLabel
 * system manages static and dynamic label mappings for network protocols such
 * as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006, 2008
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netlabel.h>
#include <net/cipso_ipv4.h>
#include <net/calipso.h>
#include <asm/bug.h>
#include <linux/atomic.h>

#include "netlabel_domainhash.h"
#include "netlabel_unlabeled.h"
#include "netlabel_cipso_v4.h"
#include "netlabel_calipso.h"
#include "netlabel_user.h"
#include "netlabel_mgmt.h"
#include "netlabel_addrlist.h"

/*
 * Configuration Functions
 */

/**
 * netlbl_cfg_map_del - Remove a NetLabel/LSM domain mapping
 * @domain: the domain mapping to remove
 * @family: address family
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes a NetLabel/LSM domain mapping.  A @domain value of NULL causes the
 * default domain mapping to be removed.  Returns zero on success, negative
 * values on failure.
 *
 */
int netlbl_cfg_map_del(const char *domain,
		       u16 family,
		       const void *addr,
		       const void *mask,
		       struct netlbl_audit *audit_info)
{
	if (addr == NULL && mask == NULL) {
		return netlbl_domhsh_remove(domain, family, audit_info);
	} else if (addr != NULL && mask != NULL) {
		switch (family) {
		case AF_INET:
			return netlbl_domhsh_remove_af4(domain, addr, mask,
							audit_info);
#if IS_ENABLED(CONFIG_IPV6)
		case AF_INET6:
			return netlbl_domhsh_remove_af6(domain, addr, mask,
							audit_info);
#endif /* IPv6 */
		default:
			return -EPFNOSUPPORT;
		}
	} else
		return -EINVAL;
}

/**
 * netlbl_cfg_unlbl_map_add - Add a new unlabeled mapping
 * @domain: the domain mapping to add
 * @family: address family
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Adds a new unlabeled NetLabel/LSM domain mapping.  A @domain value of NULL
 * causes a new default domain mapping to be added.  Returns zero on success,
 * negative values on failure.
 *
 */
int netlbl_cfg_unlbl_map_add(const char *domain,
			     u16 family,
			     const void *addr,
			     const void *mask,
			     struct netlbl_audit *audit_info)
{
	int ret_val = -ENOMEM;
	struct netlbl_dom_map *entry;
	struct netlbl_domaddr_map *addrmap = NULL;
	struct netlbl_domaddr4_map *map4 = NULL;
	struct netlbl_domaddr6_map *map6 = NULL;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL)
		return -ENOMEM;
	if (domain != NULL) {
		entry->domain = kstrdup(domain, GFP_ATOMIC);
		if (entry->domain == NULL)
			goto cfg_unlbl_map_add_failure;
	}
	entry->family = family;

	if (addr == NULL && mask == NULL)
		entry->def.type = NETLBL_NLTYPE_UNLABELED;
	else if (addr != NULL && mask != NULL) {
		addrmap = kzalloc(sizeof(*addrmap), GFP_ATOMIC);
		if (addrmap == NULL)
			goto cfg_unlbl_map_add_failure;
		INIT_LIST_HEAD(&addrmap->list4);
		INIT_LIST_HEAD(&addrmap->list6);

		switch (family) {
		case AF_INET: {
			const struct in_addr *addr4 = addr;
			const struct in_addr *mask4 = mask;
			map4 = kzalloc(sizeof(*map4), GFP_ATOMIC);
			if (map4 == NULL)
				goto cfg_unlbl_map_add_failure;
			map4->def.type = NETLBL_NLTYPE_UNLABELED;
			map4->list.addr = addr4->s_addr & mask4->s_addr;
			map4->list.mask = mask4->s_addr;
			map4->list.valid = 1;
			ret_val = netlbl_af4list_add(&map4->list,
						     &addrmap->list4);
			if (ret_val != 0)
				goto cfg_unlbl_map_add_failure;
			break;
			}
#if IS_ENABLED(CONFIG_IPV6)
		case AF_INET6: {
			const struct in6_addr *addr6 = addr;
			const struct in6_addr *mask6 = mask;
			map6 = kzalloc(sizeof(*map6), GFP_ATOMIC);
			if (map6 == NULL)
				goto cfg_unlbl_map_add_failure;
			map6->def.type = NETLBL_NLTYPE_UNLABELED;
			map6->list.addr = *addr6;
			map6->list.addr.s6_addr32[0] &= mask6->s6_addr32[0];
			map6->list.addr.s6_addr32[1] &= mask6->s6_addr32[1];
			map6->list.addr.s6_addr32[2] &= mask6->s6_addr32[2];
			map6->list.addr.s6_addr32[3] &= mask6->s6_addr32[3];
			map6->list.mask = *mask6;
			map6->list.valid = 1;
			ret_val = netlbl_af6list_add(&map6->list,
						     &addrmap->list6);
			if (ret_val != 0)
				goto cfg_unlbl_map_add_failure;
			break;
			}
#endif /* IPv6 */
		default:
			goto cfg_unlbl_map_add_failure;
		}

		entry->def.addrsel = addrmap;
		entry->def.type = NETLBL_NLTYPE_ADDRSELECT;
	} else {
		ret_val = -EINVAL;
		goto cfg_unlbl_map_add_failure;
	}

	ret_val = netlbl_domhsh_add(entry, audit_info);
	if (ret_val != 0)
		goto cfg_unlbl_map_add_failure;

	return 0;

cfg_unlbl_map_add_failure:
	kfree(entry->domain);
	kfree(entry);
	kfree(addrmap);
	kfree(map4);
	kfree(map6);
	return ret_val;
}


/**
 * netlbl_cfg_unlbl_static_add - Adds a new static label
 * @net: network namespace
 * @dev_name: interface name
 * @addr: IP address in network byte order (struct in[6]_addr)
 * @mask: address mask in network byte order (struct in[6]_addr)
 * @family: address family
 * @secid: LSM secid value for the entry
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Adds a new NetLabel static label to be used when protocol provided labels
 * are not present on incoming traffic.  If @dev_name is NULL then the default
 * interface will be used.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_cfg_unlbl_static_add(struct net *net,
				const char *dev_name,
				const void *addr,
				const void *mask,
				u16 family,
				u32 secid,
				struct netlbl_audit *audit_info)
{
	u32 addr_len;

	switch (family) {
	case AF_INET:
		addr_len = sizeof(struct in_addr);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		addr_len = sizeof(struct in6_addr);
		break;
#endif /* IPv6 */
	default:
		return -EPFNOSUPPORT;
	}

	return netlbl_unlhsh_add(net,
				 dev_name, addr, mask, addr_len,
				 secid, audit_info);
}

/**
 * netlbl_cfg_unlbl_static_del - Removes an existing static label
 * @net: network namespace
 * @dev_name: interface name
 * @addr: IP address in network byte order (struct in[6]_addr)
 * @mask: address mask in network byte order (struct in[6]_addr)
 * @family: address family
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes an existing NetLabel static label used when protocol provided labels
 * are not present on incoming traffic.  If @dev_name is NULL then the default
 * interface will be used.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_cfg_unlbl_static_del(struct net *net,
				const char *dev_name,
				const void *addr,
				const void *mask,
				u16 family,
				struct netlbl_audit *audit_info)
{
	u32 addr_len;

	switch (family) {
	case AF_INET:
		addr_len = sizeof(struct in_addr);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		addr_len = sizeof(struct in6_addr);
		break;
#endif /* IPv6 */
	default:
		return -EPFNOSUPPORT;
	}

	return netlbl_unlhsh_remove(net,
				    dev_name, addr, mask, addr_len,
				    audit_info);
}

/**
 * netlbl_cfg_cipsov4_add - Add a new CIPSOv4 DOI definition
 * @doi_def: CIPSO DOI definition
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Add a new CIPSO DOI definition as defined by @doi_def.  Returns zero on
 * success and negative values on failure.
 *
 */
int netlbl_cfg_cipsov4_add(struct cipso_v4_doi *doi_def,
			   struct netlbl_audit *audit_info)
{
	return cipso_v4_doi_add(doi_def, audit_info);
}

/**
 * netlbl_cfg_cipsov4_del - Remove an existing CIPSOv4 DOI definition
 * @doi: CIPSO DOI
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Remove an existing CIPSO DOI definition matching @doi.  Returns zero on
 * success and negative values on failure.
 *
 */
void netlbl_cfg_cipsov4_del(u32 doi, struct netlbl_audit *audit_info)
{
	cipso_v4_doi_remove(doi, audit_info);
}

/**
 * netlbl_cfg_cipsov4_map_add - Add a new CIPSOv4 DOI mapping
 * @doi: the CIPSO DOI
 * @domain: the domain mapping to add
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Add a new NetLabel/LSM domain mapping for the given CIPSO DOI to the NetLabel
 * subsystem.  A @domain value of NULL adds a new default domain mapping.
 * Returns zero on success, negative values on failure.
 *
 */
int netlbl_cfg_cipsov4_map_add(u32 doi,
			       const char *domain,
			       const struct in_addr *addr,
			       const struct in_addr *mask,
			       struct netlbl_audit *audit_info)
{
	int ret_val = -ENOMEM;
	struct cipso_v4_doi *doi_def;
	struct netlbl_dom_map *entry;
	struct netlbl_domaddr_map *addrmap = NULL;
	struct netlbl_domaddr4_map *addrinfo = NULL;

	doi_def = cipso_v4_doi_getdef(doi);
	if (doi_def == NULL)
		return -ENOENT;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL)
		goto out_entry;
	entry->family = AF_INET;
	if (domain != NULL) {
		entry->domain = kstrdup(domain, GFP_ATOMIC);
		if (entry->domain == NULL)
			goto out_domain;
	}

	if (addr == NULL && mask == NULL) {
		entry->def.cipso = doi_def;
		entry->def.type = NETLBL_NLTYPE_CIPSOV4;
	} else if (addr != NULL && mask != NULL) {
		addrmap = kzalloc(sizeof(*addrmap), GFP_ATOMIC);
		if (addrmap == NULL)
			goto out_addrmap;
		INIT_LIST_HEAD(&addrmap->list4);
		INIT_LIST_HEAD(&addrmap->list6);

		addrinfo = kzalloc(sizeof(*addrinfo), GFP_ATOMIC);
		if (addrinfo == NULL)
			goto out_addrinfo;
		addrinfo->def.cipso = doi_def;
		addrinfo->def.type = NETLBL_NLTYPE_CIPSOV4;
		addrinfo->list.addr = addr->s_addr & mask->s_addr;
		addrinfo->list.mask = mask->s_addr;
		addrinfo->list.valid = 1;
		ret_val = netlbl_af4list_add(&addrinfo->list, &addrmap->list4);
		if (ret_val != 0)
			goto cfg_cipsov4_map_add_failure;

		entry->def.addrsel = addrmap;
		entry->def.type = NETLBL_NLTYPE_ADDRSELECT;
	} else {
		ret_val = -EINVAL;
		goto out_addrmap;
	}

	ret_val = netlbl_domhsh_add(entry, audit_info);
	if (ret_val != 0)
		goto cfg_cipsov4_map_add_failure;

	return 0;

cfg_cipsov4_map_add_failure:
	kfree(addrinfo);
out_addrinfo:
	kfree(addrmap);
out_addrmap:
	kfree(entry->domain);
out_domain:
	kfree(entry);
out_entry:
	cipso_v4_doi_putdef(doi_def);
	return ret_val;
}

/**
 * netlbl_cfg_calipso_add - Add a new CALIPSO DOI definition
 * @doi_def: CALIPSO DOI definition
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Add a new CALIPSO DOI definition as defined by @doi_def.  Returns zero on
 * success and negative values on failure.
 *
 */
int netlbl_cfg_calipso_add(struct calipso_doi *doi_def,
			   struct netlbl_audit *audit_info)
{
#if IS_ENABLED(CONFIG_IPV6)
	return calipso_doi_add(doi_def, audit_info);
#else /* IPv6 */
	return -ENOSYS;
#endif /* IPv6 */
}

/**
 * netlbl_cfg_calipso_del - Remove an existing CALIPSO DOI definition
 * @doi: CALIPSO DOI
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Remove an existing CALIPSO DOI definition matching @doi.  Returns zero on
 * success and negative values on failure.
 *
 */
void netlbl_cfg_calipso_del(u32 doi, struct netlbl_audit *audit_info)
{
#if IS_ENABLED(CONFIG_IPV6)
	calipso_doi_remove(doi, audit_info);
#endif /* IPv6 */
}

/**
 * netlbl_cfg_calipso_map_add - Add a new CALIPSO DOI mapping
 * @doi: the CALIPSO DOI
 * @domain: the domain mapping to add
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Add a new NetLabel/LSM domain mapping for the given CALIPSO DOI to the
 * NetLabel subsystem.  A @domain value of NULL adds a new default domain
 * mapping.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_cfg_calipso_map_add(u32 doi,
			       const char *domain,
			       const struct in6_addr *addr,
			       const struct in6_addr *mask,
			       struct netlbl_audit *audit_info)
{
#if IS_ENABLED(CONFIG_IPV6)
	int ret_val = -ENOMEM;
	struct calipso_doi *doi_def;
	struct netlbl_dom_map *entry;
	struct netlbl_domaddr_map *addrmap = NULL;
	struct netlbl_domaddr6_map *addrinfo = NULL;

	doi_def = calipso_doi_getdef(doi);
	if (doi_def == NULL)
		return -ENOENT;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL)
		goto out_entry;
	entry->family = AF_INET6;
	if (domain != NULL) {
		entry->domain = kstrdup(domain, GFP_ATOMIC);
		if (entry->domain == NULL)
			goto out_domain;
	}

	if (addr == NULL && mask == NULL) {
		entry->def.calipso = doi_def;
		entry->def.type = NETLBL_NLTYPE_CALIPSO;
	} else if (addr != NULL && mask != NULL) {
		addrmap = kzalloc(sizeof(*addrmap), GFP_ATOMIC);
		if (addrmap == NULL)
			goto out_addrmap;
		INIT_LIST_HEAD(&addrmap->list4);
		INIT_LIST_HEAD(&addrmap->list6);

		addrinfo = kzalloc(sizeof(*addrinfo), GFP_ATOMIC);
		if (addrinfo == NULL)
			goto out_addrinfo;
		addrinfo->def.calipso = doi_def;
		addrinfo->def.type = NETLBL_NLTYPE_CALIPSO;
		addrinfo->list.addr = *addr;
		addrinfo->list.addr.s6_addr32[0] &= mask->s6_addr32[0];
		addrinfo->list.addr.s6_addr32[1] &= mask->s6_addr32[1];
		addrinfo->list.addr.s6_addr32[2] &= mask->s6_addr32[2];
		addrinfo->list.addr.s6_addr32[3] &= mask->s6_addr32[3];
		addrinfo->list.mask = *mask;
		addrinfo->list.valid = 1;
		ret_val = netlbl_af6list_add(&addrinfo->list, &addrmap->list6);
		if (ret_val != 0)
			goto cfg_calipso_map_add_failure;

		entry->def.addrsel = addrmap;
		entry->def.type = NETLBL_NLTYPE_ADDRSELECT;
	} else {
		ret_val = -EINVAL;
		goto out_addrmap;
	}

	ret_val = netlbl_domhsh_add(entry, audit_info);
	if (ret_val != 0)
		goto cfg_calipso_map_add_failure;

	return 0;

cfg_calipso_map_add_failure:
	kfree(addrinfo);
out_addrinfo:
	kfree(addrmap);
out_addrmap:
	kfree(entry->domain);
out_domain:
	kfree(entry);
out_entry:
	calipso_doi_putdef(doi_def);
	return ret_val;
#else /* IPv6 */
	return -ENOSYS;
#endif /* IPv6 */
}

/*
 * Security Attribute Functions
 */

#define _CM_F_NONE	0x00000000
#define _CM_F_ALLOC	0x00000001
#define _CM_F_WALK	0x00000002

/**
 * _netlbl_catmap_getnode - Get a individual node from a catmap
 * @catmap: pointer to the category bitmap
 * @offset: the requested offset
 * @cm_flags: catmap flags, see _CM_F_*
 * @gfp_flags: memory allocation flags
 *
 * Description:
 * Iterate through the catmap looking for the node associated with @offset.
 * If the _CM_F_ALLOC flag is set in @cm_flags and there is no associated node,
 * one will be created and inserted into the catmap.  If the _CM_F_WALK flag is
 * set in @cm_flags and there is no associated node, the next highest node will
 * be returned.  Returns a pointer to the node on success, NULL on failure.
 *
 */
static struct netlbl_lsm_catmap *_netlbl_catmap_getnode(
					     struct netlbl_lsm_catmap **catmap,
					     u32 offset,
					     unsigned int cm_flags,
					     gfp_t gfp_flags)
{
	struct netlbl_lsm_catmap *iter = *catmap;
	struct netlbl_lsm_catmap *prev = NULL;

	if (iter == NULL)
		goto catmap_getnode_alloc;
	if (offset < iter->startbit)
		goto catmap_getnode_walk;
	while (iter && offset >= (iter->startbit + NETLBL_CATMAP_SIZE)) {
		prev = iter;
		iter = iter->next;
	}
	if (iter == NULL || offset < iter->startbit)
		goto catmap_getnode_walk;

	return iter;

catmap_getnode_walk:
	if (cm_flags & _CM_F_WALK)
		return iter;
catmap_getnode_alloc:
	if (!(cm_flags & _CM_F_ALLOC))
		return NULL;

	iter = netlbl_catmap_alloc(gfp_flags);
	if (iter == NULL)
		return NULL;
	iter->startbit = offset & ~(NETLBL_CATMAP_SIZE - 1);

	if (prev == NULL) {
		iter->next = *catmap;
		*catmap = iter;
	} else {
		iter->next = prev->next;
		prev->next = iter;
	}

	return iter;
}

/**
 * netlbl_catmap_walk - Walk a LSM secattr catmap looking for a bit
 * @catmap: the category bitmap
 * @offset: the offset to start searching at, in bits
 *
 * Description:
 * This function walks a LSM secattr category bitmap starting at @offset and
 * returns the spot of the first set bit or -ENOENT if no bits are set.
 *
 */
int netlbl_catmap_walk(struct netlbl_lsm_catmap *catmap, u32 offset)
{
	struct netlbl_lsm_catmap *iter;
	u32 idx;
	u32 bit;
	NETLBL_CATMAP_MAPTYPE bitmap;

	iter = _netlbl_catmap_getnode(&catmap, offset, _CM_F_WALK, 0);
	if (iter == NULL)
		return -ENOENT;
	if (offset > iter->startbit) {
		offset -= iter->startbit;
		idx = offset / NETLBL_CATMAP_MAPSIZE;
		bit = offset % NETLBL_CATMAP_MAPSIZE;
	} else {
		idx = 0;
		bit = 0;
	}
	bitmap = iter->bitmap[idx] >> bit;

	for (;;) {
		if (bitmap != 0) {
			while ((bitmap & NETLBL_CATMAP_BIT) == 0) {
				bitmap >>= 1;
				bit++;
			}
			return iter->startbit +
			       (NETLBL_CATMAP_MAPSIZE * idx) + bit;
		}
		if (++idx >= NETLBL_CATMAP_MAPCNT) {
			if (iter->next != NULL) {
				iter = iter->next;
				idx = 0;
			} else
				return -ENOENT;
		}
		bitmap = iter->bitmap[idx];
		bit = 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(netlbl_catmap_walk);

/**
 * netlbl_catmap_walkrng - Find the end of a string of set bits
 * @catmap: the category bitmap
 * @offset: the offset to start searching at, in bits
 *
 * Description:
 * This function walks a LSM secattr category bitmap starting at @offset and
 * returns the spot of the first cleared bit or -ENOENT if the offset is past
 * the end of the bitmap.
 *
 */
int netlbl_catmap_walkrng(struct netlbl_lsm_catmap *catmap, u32 offset)
{
	struct netlbl_lsm_catmap *iter;
	struct netlbl_lsm_catmap *prev = NULL;
	u32 idx;
	u32 bit;
	NETLBL_CATMAP_MAPTYPE bitmask;
	NETLBL_CATMAP_MAPTYPE bitmap;

	iter = _netlbl_catmap_getnode(&catmap, offset, _CM_F_WALK, 0);
	if (iter == NULL)
		return -ENOENT;
	if (offset > iter->startbit) {
		offset -= iter->startbit;
		idx = offset / NETLBL_CATMAP_MAPSIZE;
		bit = offset % NETLBL_CATMAP_MAPSIZE;
	} else {
		idx = 0;
		bit = 0;
	}
	bitmask = NETLBL_CATMAP_BIT << bit;

	for (;;) {
		bitmap = iter->bitmap[idx];
		while (bitmask != 0 && (bitmap & bitmask) != 0) {
			bitmask <<= 1;
			bit++;
		}

		if (prev && idx == 0 && bit == 0)
			return prev->startbit + NETLBL_CATMAP_SIZE - 1;
		else if (bitmask != 0)
			return iter->startbit +
				(NETLBL_CATMAP_MAPSIZE * idx) + bit - 1;
		else if (++idx >= NETLBL_CATMAP_MAPCNT) {
			if (iter->next == NULL)
				return iter->startbit + NETLBL_CATMAP_SIZE - 1;
			prev = iter;
			iter = iter->next;
			idx = 0;
		}
		bitmask = NETLBL_CATMAP_BIT;
		bit = 0;
	}

	return -ENOENT;
}

/**
 * netlbl_catmap_getlong - Export an unsigned long bitmap
 * @catmap: pointer to the category bitmap
 * @offset: pointer to the requested offset
 * @bitmap: the exported bitmap
 *
 * Description:
 * Export a bitmap with an offset greater than or equal to @offset and return
 * it in @bitmap.  The @offset must be aligned to an unsigned long and will be
 * updated on return if different from what was requested; if the catmap is
 * empty at the requested offset and beyond, the @offset is set to (u32)-1.
 * Returns zero on success, negative values on failure.
 *
 */
int netlbl_catmap_getlong(struct netlbl_lsm_catmap *catmap,
			  u32 *offset,
			  unsigned long *bitmap)
{
	struct netlbl_lsm_catmap *iter;
	u32 off = *offset;
	u32 idx;

	/* only allow aligned offsets */
	if ((off & (BITS_PER_LONG - 1)) != 0)
		return -EINVAL;

	/* a null catmap is equivalent to an empty one */
	if (!catmap) {
		*offset = (u32)-1;
		return 0;
	}

	if (off < catmap->startbit) {
		off = catmap->startbit;
		*offset = off;
	}
	iter = _netlbl_catmap_getnode(&catmap, off, _CM_F_WALK, 0);
	if (iter == NULL) {
		*offset = (u32)-1;
		return 0;
	}

	if (off < iter->startbit) {
		*offset = iter->startbit;
		off = 0;
	} else
		off -= iter->startbit;
	idx = off / NETLBL_CATMAP_MAPSIZE;
	*bitmap = iter->bitmap[idx] >> (off % NETLBL_CATMAP_MAPSIZE);

	return 0;
}

/**
 * netlbl_catmap_setbit - Set a bit in a LSM secattr catmap
 * @catmap: pointer to the category bitmap
 * @bit: the bit to set
 * @flags: memory allocation flags
 *
 * Description:
 * Set the bit specified by @bit in @catmap.  Returns zero on success,
 * negative values on failure.
 *
 */
int netlbl_catmap_setbit(struct netlbl_lsm_catmap **catmap,
			 u32 bit,
			 gfp_t flags)
{
	struct netlbl_lsm_catmap *iter;
	u32 idx;

	iter = _netlbl_catmap_getnode(catmap, bit, _CM_F_ALLOC, flags);
	if (iter == NULL)
		return -ENOMEM;

	bit -= iter->startbit;
	idx = bit / NETLBL_CATMAP_MAPSIZE;
	iter->bitmap[idx] |= NETLBL_CATMAP_BIT << (bit % NETLBL_CATMAP_MAPSIZE);

	return 0;
}
EXPORT_SYMBOL(netlbl_catmap_setbit);

/**
 * netlbl_catmap_setrng - Set a range of bits in a LSM secattr catmap
 * @catmap: pointer to the category bitmap
 * @start: the starting bit
 * @end: the last bit in the string
 * @flags: memory allocation flags
 *
 * Description:
 * Set a range of bits, starting at @start and ending with @end.  Returns zero
 * on success, negative values on failure.
 *
 */
int netlbl_catmap_setrng(struct netlbl_lsm_catmap **catmap,
			 u32 start,
			 u32 end,
			 gfp_t flags)
{
	int rc = 0;
	u32 spot = start;

	while (rc == 0 && spot <= end) {
		if (((spot & (BITS_PER_LONG - 1)) == 0) &&
		    ((end - spot) > BITS_PER_LONG)) {
			rc = netlbl_catmap_setlong(catmap,
						   spot,
						   (unsigned long)-1,
						   flags);
			spot += BITS_PER_LONG;
		} else
			rc = netlbl_catmap_setbit(catmap, spot++, flags);
	}

	return rc;
}

/**
 * netlbl_catmap_setlong - Import an unsigned long bitmap
 * @catmap: pointer to the category bitmap
 * @offset: offset to the start of the imported bitmap
 * @bitmap: the bitmap to import
 * @flags: memory allocation flags
 *
 * Description:
 * Import the bitmap specified in @bitmap into @catmap, using the offset
 * in @offset.  The offset must be aligned to an unsigned long.  Returns zero
 * on success, negative values on failure.
 *
 */
int netlbl_catmap_setlong(struct netlbl_lsm_catmap **catmap,
			  u32 offset,
			  unsigned long bitmap,
			  gfp_t flags)
{
	struct netlbl_lsm_catmap *iter;
	u32 idx;

	/* only allow aligned offsets */
	if ((offset & (BITS_PER_LONG - 1)) != 0)
		return -EINVAL;

	iter = _netlbl_catmap_getnode(catmap, offset, _CM_F_ALLOC, flags);
	if (iter == NULL)
		return -ENOMEM;

	offset -= iter->startbit;
	idx = offset / NETLBL_CATMAP_MAPSIZE;
	iter->bitmap[idx] |= (NETLBL_CATMAP_MAPTYPE)bitmap
			     << (offset % NETLBL_CATMAP_MAPSIZE);

	return 0;
}

/* Bitmap functions
 */

/**
 * netlbl_bitmap_walk - Walk a bitmap looking for a bit
 * @bitmap: the bitmap
 * @bitmap_len: length in bits
 * @offset: starting offset
 * @state: if non-zero, look for a set (1) bit else look for a cleared (0) bit
 *
 * Description:
 * Starting at @offset, walk the bitmap from left to right until either the
 * desired bit is found or we reach the end.  Return the bit offset, -1 if
 * not found, or -2 if error.
 */
int netlbl_bitmap_walk(const unsigned char *bitmap, u32 bitmap_len,
		       u32 offset, u8 state)
{
	u32 bit_spot;
	u32 byte_offset;
	unsigned char bitmask;
	unsigned char byte;

	if (offset >= bitmap_len)
		return -1;
	byte_offset = offset / 8;
	byte = bitmap[byte_offset];
	bit_spot = offset;
	bitmask = 0x80 >> (offset % 8);

	while (bit_spot < bitmap_len) {
		if ((state && (byte & bitmask) == bitmask) ||
		    (state == 0 && (byte & bitmask) == 0))
			return bit_spot;

		if (++bit_spot >= bitmap_len)
			return -1;
		bitmask >>= 1;
		if (bitmask == 0) {
			byte = bitmap[++byte_offset];
			bitmask = 0x80;
		}
	}

	return -1;
}
EXPORT_SYMBOL(netlbl_bitmap_walk);

/**
 * netlbl_bitmap_setbit - Sets a single bit in a bitmap
 * @bitmap: the bitmap
 * @bit: the bit
 * @state: if non-zero, set the bit (1) else clear the bit (0)
 *
 * Description:
 * Set a single bit in the bitmask.  Returns zero on success, negative values
 * on error.
 */
void netlbl_bitmap_setbit(unsigned char *bitmap, u32 bit, u8 state)
{
	u32 byte_spot;
	u8 bitmask;

	/* gcc always rounds to zero when doing integer division */
	byte_spot = bit / 8;
	bitmask = 0x80 >> (bit % 8);
	if (state)
		bitmap[byte_spot] |= bitmask;
	else
		bitmap[byte_spot] &= ~bitmask;
}
EXPORT_SYMBOL(netlbl_bitmap_setbit);

/*
 * LSM Functions
 */

/**
 * netlbl_enabled - Determine if the NetLabel subsystem is enabled
 *
 * Description:
 * The LSM can use this function to determine if it should use NetLabel
 * security attributes in it's enforcement mechanism.  Currently, NetLabel is
 * considered to be enabled when it's configuration contains a valid setup for
 * at least one labeled protocol (i.e. NetLabel can understand incoming
 * labeled packets of at least one type); otherwise NetLabel is considered to
 * be disabled.
 *
 */
int netlbl_enabled(void)
{
	/* At some point we probably want to expose this mechanism to the user
	 * as well so that admins can toggle NetLabel regardless of the
	 * configuration */
	return (atomic_read(&netlabel_mgmt_protocount) > 0);
}

/**
 * netlbl_sock_setattr - Label a socket using the correct protocol
 * @sk: the socket to label
 * @family: protocol family
 * @secattr: the security attributes
 *
 * Description:
 * Attach the correct label to the given socket using the security attributes
 * specified in @secattr.  This function requires exclusive access to @sk,
 * which means it either needs to be in the process of being created or locked.
 * Returns zero on success, -EDESTADDRREQ if the domain is configured to use
 * network address selectors (can't blindly label the socket), and negative
 * values on all other failures.
 *
 */
int netlbl_sock_setattr(struct sock *sk,
			u16 family,
			const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct netlbl_dom_map *dom_entry;

	rcu_read_lock();
	dom_entry = netlbl_domhsh_getentry(secattr->domain, family);
	if (dom_entry == NULL) {
		ret_val = -ENOENT;
		goto socket_setattr_return;
	}
	switch (family) {
	case AF_INET:
		switch (dom_entry->def.type) {
		case NETLBL_NLTYPE_ADDRSELECT:
			ret_val = -EDESTADDRREQ;
			break;
		case NETLBL_NLTYPE_CIPSOV4:
			ret_val = cipso_v4_sock_setattr(sk,
							dom_entry->def.cipso,
							secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		switch (dom_entry->def.type) {
		case NETLBL_NLTYPE_ADDRSELECT:
			ret_val = -EDESTADDRREQ;
			break;
		case NETLBL_NLTYPE_CALIPSO:
			ret_val = calipso_sock_setattr(sk,
						       dom_entry->def.calipso,
						       secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#endif /* IPv6 */
	default:
		ret_val = -EPROTONOSUPPORT;
	}

socket_setattr_return:
	rcu_read_unlock();
	return ret_val;
}

/**
 * netlbl_sock_delattr - Delete all the NetLabel labels on a socket
 * @sk: the socket
 *
 * Description:
 * Remove all the NetLabel labeling from @sk.  The caller is responsible for
 * ensuring that @sk is locked.
 *
 */
void netlbl_sock_delattr(struct sock *sk)
{
	switch (sk->sk_family) {
	case AF_INET:
		cipso_v4_sock_delattr(sk);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		calipso_sock_delattr(sk);
		break;
#endif /* IPv6 */
	}
}

/**
 * netlbl_sock_getattr - Determine the security attributes of a sock
 * @sk: the sock
 * @secattr: the security attributes
 *
 * Description:
 * Examines the given sock to see if any NetLabel style labeling has been
 * applied to the sock, if so it parses the socket label and returns the
 * security attributes in @secattr.  Returns zero on success, negative values
 * on failure.
 *
 */
int netlbl_sock_getattr(struct sock *sk,
			struct netlbl_lsm_secattr *secattr)
{
	int ret_val;

	switch (sk->sk_family) {
	case AF_INET:
		ret_val = cipso_v4_sock_getattr(sk, secattr);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		ret_val = calipso_sock_getattr(sk, secattr);
		break;
#endif /* IPv6 */
	default:
		ret_val = -EPROTONOSUPPORT;
	}

	return ret_val;
}

/**
 * netlbl_conn_setattr - Label a connected socket using the correct protocol
 * @sk: the socket to label
 * @addr: the destination address
 * @secattr: the security attributes
 *
 * Description:
 * Attach the correct label to the given connected socket using the security
 * attributes specified in @secattr.  The caller is responsible for ensuring
 * that @sk is locked.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_conn_setattr(struct sock *sk,
			struct sockaddr *addr,
			const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct sockaddr_in *addr4;
#if IS_ENABLED(CONFIG_IPV6)
	struct sockaddr_in6 *addr6;
#endif
	struct netlbl_dommap_def *entry;

	rcu_read_lock();
	switch (addr->sa_family) {
	case AF_INET:
		addr4 = (struct sockaddr_in *)addr;
		entry = netlbl_domhsh_getentry_af4(secattr->domain,
						   addr4->sin_addr.s_addr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto conn_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CIPSOV4:
			ret_val = cipso_v4_sock_setattr(sk,
							entry->cipso, secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			/* just delete the protocols we support for right now
			 * but we could remove other protocols if needed */
			netlbl_sock_delattr(sk);
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		addr6 = (struct sockaddr_in6 *)addr;
		entry = netlbl_domhsh_getentry_af6(secattr->domain,
						   &addr6->sin6_addr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto conn_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CALIPSO:
			ret_val = calipso_sock_setattr(sk,
						       entry->calipso, secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			/* just delete the protocols we support for right now
			 * but we could remove other protocols if needed */
			netlbl_sock_delattr(sk);
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#endif /* IPv6 */
	default:
		ret_val = -EPROTONOSUPPORT;
	}

conn_setattr_return:
	rcu_read_unlock();
	return ret_val;
}

/**
 * netlbl_req_setattr - Label a request socket using the correct protocol
 * @req: the request socket to label
 * @secattr: the security attributes
 *
 * Description:
 * Attach the correct label to the given socket using the security attributes
 * specified in @secattr.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_req_setattr(struct request_sock *req,
		       const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct netlbl_dommap_def *entry;
	struct inet_request_sock *ireq = inet_rsk(req);

	rcu_read_lock();
	switch (req->rsk_ops->family) {
	case AF_INET:
		entry = netlbl_domhsh_getentry_af4(secattr->domain,
						   ireq->ir_rmt_addr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto req_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CIPSOV4:
			ret_val = cipso_v4_req_setattr(req,
						       entry->cipso, secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			netlbl_req_delattr(req);
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		entry = netlbl_domhsh_getentry_af6(secattr->domain,
						   &ireq->ir_v6_rmt_addr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto req_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CALIPSO:
			ret_val = calipso_req_setattr(req,
						      entry->calipso, secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			netlbl_req_delattr(req);
			ret_val = 0;
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#endif /* IPv6 */
	default:
		ret_val = -EPROTONOSUPPORT;
	}

req_setattr_return:
	rcu_read_unlock();
	return ret_val;
}

/**
* netlbl_req_delattr - Delete all the NetLabel labels on a socket
* @req: the socket
*
* Description:
* Remove all the NetLabel labeling from @req.
*
*/
void netlbl_req_delattr(struct request_sock *req)
{
	switch (req->rsk_ops->family) {
	case AF_INET:
		cipso_v4_req_delattr(req);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		calipso_req_delattr(req);
		break;
#endif /* IPv6 */
	}
}

/**
 * netlbl_skbuff_setattr - Label a packet using the correct protocol
 * @skb: the packet
 * @family: protocol family
 * @secattr: the security attributes
 *
 * Description:
 * Attach the correct label to the given packet using the security attributes
 * specified in @secattr.  Returns zero on success, negative values on failure.
 *
 */
int netlbl_skbuff_setattr(struct sk_buff *skb,
			  u16 family,
			  const struct netlbl_lsm_secattr *secattr)
{
	int ret_val;
	struct iphdr *hdr4;
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6hdr *hdr6;
#endif
	struct netlbl_dommap_def *entry;

	rcu_read_lock();
	switch (family) {
	case AF_INET:
		hdr4 = ip_hdr(skb);
		entry = netlbl_domhsh_getentry_af4(secattr->domain,
						   hdr4->daddr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto skbuff_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CIPSOV4:
			ret_val = cipso_v4_skbuff_setattr(skb, entry->cipso,
							  secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			/* just delete the protocols we support for right now
			 * but we could remove other protocols if needed */
			ret_val = cipso_v4_skbuff_delattr(skb);
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		hdr6 = ipv6_hdr(skb);
		entry = netlbl_domhsh_getentry_af6(secattr->domain,
						   &hdr6->daddr);
		if (entry == NULL) {
			ret_val = -ENOENT;
			goto skbuff_setattr_return;
		}
		switch (entry->type) {
		case NETLBL_NLTYPE_CALIPSO:
			ret_val = calipso_skbuff_setattr(skb, entry->calipso,
							 secattr);
			break;
		case NETLBL_NLTYPE_UNLABELED:
			/* just delete the protocols we support for right now
			 * but we could remove other protocols if needed */
			ret_val = calipso_skbuff_delattr(skb);
			break;
		default:
			ret_val = -ENOENT;
		}
		break;
#endif /* IPv6 */
	default:
		ret_val = -EPROTONOSUPPORT;
	}

skbuff_setattr_return:
	rcu_read_unlock();
	return ret_val;
}

/**
 * netlbl_skbuff_getattr - Determine the security attributes of a packet
 * @skb: the packet
 * @family: protocol family
 * @secattr: the security attributes
 *
 * Description:
 * Examines the given packet to see if a recognized form of packet labeling
 * is present, if so it parses the packet label and returns the security
 * attributes in @secattr.  Returns zero on success, negative values on
 * failure.
 *
 */
int netlbl_skbuff_getattr(const struct sk_buff *skb,
			  u16 family,
			  struct netlbl_lsm_secattr *secattr)
{
	unsigned char *ptr;

	switch (family) {
	case AF_INET:
		ptr = cipso_v4_optptr(skb);
		if (ptr && cipso_v4_getattr(ptr, secattr) == 0)
			return 0;
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		ptr = calipso_optptr(skb);
		if (ptr && calipso_getattr(ptr, secattr) == 0)
			return 0;
		break;
#endif /* IPv6 */
	}

	return netlbl_unlabel_getattr(skb, family, secattr);
}

/**
 * netlbl_skbuff_err - Handle a LSM error on a sk_buff
 * @skb: the packet
 * @family: the family
 * @error: the error code
 * @gateway: true if host is acting as a gateway, false otherwise
 *
 * Description:
 * Deal with a LSM problem when handling the packet in @skb, typically this is
 * a permission denied problem (-EACCES).  The correct action is determined
 * according to the packet's labeling protocol.
 *
 */
void netlbl_skbuff_err(struct sk_buff *skb, u16 family, int error, int gateway)
{
	switch (family) {
	case AF_INET:
		if (cipso_v4_optptr(skb))
			cipso_v4_error(skb, error, gateway);
		break;
	}
}

/**
 * netlbl_cache_invalidate - Invalidate all of the NetLabel protocol caches
 *
 * Description:
 * For all of the NetLabel protocols that support some form of label mapping
 * cache, invalidate the cache.  Returns zero on success, negative values on
 * error.
 *
 */
void netlbl_cache_invalidate(void)
{
	cipso_v4_cache_invalidate();
#if IS_ENABLED(CONFIG_IPV6)
	calipso_cache_invalidate();
#endif /* IPv6 */
}

/**
 * netlbl_cache_add - Add an entry to a NetLabel protocol cache
 * @skb: the packet
 * @family: the family
 * @secattr: the packet's security attributes
 *
 * Description:
 * Add the LSM security attributes for the given packet to the underlying
 * NetLabel protocol's label mapping cache.  Returns zero on success, negative
 * values on error.
 *
 */
int netlbl_cache_add(const struct sk_buff *skb, u16 family,
		     const struct netlbl_lsm_secattr *secattr)
{
	unsigned char *ptr;

	if ((secattr->flags & NETLBL_SECATTR_CACHE) == 0)
		return -ENOMSG;

	switch (family) {
	case AF_INET:
		ptr = cipso_v4_optptr(skb);
		if (ptr)
			return cipso_v4_cache_add(ptr, secattr);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		ptr = calipso_optptr(skb);
		if (ptr)
			return calipso_cache_add(ptr, secattr);
		break;
#endif /* IPv6 */
	}
	return -ENOMSG;
}

/*
 * Protocol Engine Functions
 */

/**
 * netlbl_audit_start - Start an audit message
 * @type: audit message type
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Start an audit message using the type specified in @type and fill the audit
 * message with some fields common to all NetLabel audit messages.  This
 * function should only be used by protocol engines, not LSMs.  Returns a
 * pointer to the audit buffer on success, NULL on failure.
 *
 */
struct audit_buffer *netlbl_audit_start(int type,
					struct netlbl_audit *audit_info)
{
	return netlbl_audit_start_common(type, audit_info);
}
EXPORT_SYMBOL(netlbl_audit_start);

/*
 * Setup Functions
 */

/**
 * netlbl_init - Initialize NetLabel
 *
 * Description:
 * Perform the required NetLabel initialization before first use.
 *
 */
static int __init netlbl_init(void)
{
	int ret_val;

	printk(KERN_INFO "NetLabel: Initializing\n");
	printk(KERN_INFO "NetLabel:  domain hash size = %u\n",
	       (1 << NETLBL_DOMHSH_BITSIZE));
	printk(KERN_INFO "NetLabel:  protocols = UNLABELED CIPSOv4 CALIPSO\n");

	ret_val = netlbl_domhsh_init(NETLBL_DOMHSH_BITSIZE);
	if (ret_val != 0)
		goto init_failure;

	ret_val = netlbl_unlabel_init(NETLBL_UNLHSH_BITSIZE);
	if (ret_val != 0)
		goto init_failure;

	ret_val = netlbl_netlink_init();
	if (ret_val != 0)
		goto init_failure;

	ret_val = netlbl_unlabel_defconf();
	if (ret_val != 0)
		goto init_failure;
	printk(KERN_INFO "NetLabel:  unlabeled traffic allowed by default\n");

	return 0;

init_failure:
	panic("NetLabel: failed to initialize properly (%d)\n", ret_val);
}

subsys_initcall(netlbl_init);
