/*
 * NetLabel System
 *
 * The NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef _NETLABEL_H
#define _NETLABEL_H

#include <linux/types.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <net/netlink.h>
#include <asm/atomic.h>

struct cipso_v4_doi;

/*
 * NetLabel - A management interface for maintaining network packet label
 *            mapping tables for explicit packet labling protocols.
 *
 * Network protocols such as CIPSO and RIPSO require a label translation layer
 * to convert the label on the packet into something meaningful on the host
 * machine.  In the current Linux implementation these mapping tables live
 * inside the kernel; NetLabel provides a mechanism for user space applications
 * to manage these mapping tables.
 *
 * NetLabel makes use of the Generic NETLINK mechanism as a transport layer to
 * send messages between kernel and user space.  The general format of a
 * NetLabel message is shown below:
 *
 *  +-----------------+-------------------+--------- --- -- -
 *  | struct nlmsghdr | struct genlmsghdr | payload
 *  +-----------------+-------------------+--------- --- -- -
 *
 * The 'nlmsghdr' and 'genlmsghdr' structs should be dealt with like normal.
 * The payload is dependent on the subsystem specified in the
 * 'nlmsghdr->nlmsg_type' and should be defined below, supporting functions
 * should be defined in the corresponding net/netlabel/netlabel_<subsys>.h|c
 * file.  All of the fields in the NetLabel payload are NETLINK attributes, see
 * the include/net/netlink.h file for more information on NETLINK attributes.
 *
 */

/*
 * NetLabel NETLINK protocol
 */

/* NetLabel NETLINK protocol version
 *  1: initial version
 *  2: added static labels for unlabeled connections
 */
#define NETLBL_PROTO_VERSION            2

/* NetLabel NETLINK types/families */
#define NETLBL_NLTYPE_NONE              0
#define NETLBL_NLTYPE_MGMT              1
#define NETLBL_NLTYPE_MGMT_NAME         "NLBL_MGMT"
#define NETLBL_NLTYPE_RIPSO             2
#define NETLBL_NLTYPE_RIPSO_NAME        "NLBL_RIPSO"
#define NETLBL_NLTYPE_CIPSOV4           3
#define NETLBL_NLTYPE_CIPSOV4_NAME      "NLBL_CIPSOv4"
#define NETLBL_NLTYPE_CIPSOV6           4
#define NETLBL_NLTYPE_CIPSOV6_NAME      "NLBL_CIPSOv6"
#define NETLBL_NLTYPE_UNLABELED         5
#define NETLBL_NLTYPE_UNLABELED_NAME    "NLBL_UNLBL"

/*
 * NetLabel - Kernel API for accessing the network packet label mappings.
 *
 * The following functions are provided for use by other kernel modules,
 * specifically kernel LSM modules, to provide a consistent, transparent API
 * for dealing with explicit packet labeling protocols such as CIPSO and
 * RIPSO.  The functions defined here are implemented in the
 * net/netlabel/netlabel_kapi.c file.
 *
 */

/* NetLabel audit information */
struct netlbl_audit {
	u32 secid;
	uid_t loginuid;
	u32 sessionid;
};

/*
 * LSM security attributes
 */

/**
 * struct netlbl_lsm_cache - NetLabel LSM security attribute cache
 * @refcount: atomic reference counter
 * @free: LSM supplied function to free the cache data
 * @data: LSM supplied cache data
 *
 * Description:
 * This structure is provided for LSMs which wish to make use of the NetLabel
 * caching mechanism to store LSM specific data/attributes in the NetLabel
 * cache.  If the LSM has to perform a lot of translation from the NetLabel
 * security attributes into it's own internal representation then the cache
 * mechanism can provide a way to eliminate some or all of that translation
 * overhead on a cache hit.
 *
 */
struct netlbl_lsm_cache {
	atomic_t refcount;
	void (*free) (const void *data);
	void *data;
};

/**
 * struct netlbl_lsm_secattr_catmap - NetLabel LSM secattr category bitmap
 * @startbit: the value of the lowest order bit in the bitmap
 * @bitmap: the category bitmap
 * @next: pointer to the next bitmap "node" or NULL
 *
 * Description:
 * This structure is used to represent category bitmaps.  Due to the large
 * number of categories supported by most labeling protocols it is not
 * practical to transfer a full bitmap internally so NetLabel adopts a sparse
 * bitmap structure modeled after SELinux's ebitmap structure.
 * The catmap bitmap field MUST be a power of two in length and large
 * enough to hold at least 240 bits.  Special care (i.e. check the code!)
 * should be used when changing these values as the LSM implementation
 * probably has functions which rely on the sizes of these types to speed
 * processing.
 *
 */
#define NETLBL_CATMAP_MAPTYPE           u64
#define NETLBL_CATMAP_MAPCNT            4
#define NETLBL_CATMAP_MAPSIZE           (sizeof(NETLBL_CATMAP_MAPTYPE) * 8)
#define NETLBL_CATMAP_SIZE              (NETLBL_CATMAP_MAPSIZE * \
					 NETLBL_CATMAP_MAPCNT)
#define NETLBL_CATMAP_BIT               (NETLBL_CATMAP_MAPTYPE)0x01
struct netlbl_lsm_secattr_catmap {
	u32 startbit;
	NETLBL_CATMAP_MAPTYPE bitmap[NETLBL_CATMAP_MAPCNT];
	struct netlbl_lsm_secattr_catmap *next;
};

/**
 * struct netlbl_lsm_secattr - NetLabel LSM security attributes
 * @flags: indicate structure attributes, see NETLBL_SECATTR_*
 * @type: indicate the NLTYPE of the attributes
 * @domain: the NetLabel LSM domain
 * @cache: NetLabel LSM specific cache
 * @attr.mls: MLS sensitivity label
 * @attr.mls.cat: MLS category bitmap
 * @attr.mls.lvl: MLS sensitivity level
 * @attr.secid: LSM specific secid token
 *
 * Description:
 * This structure is used to pass security attributes between NetLabel and the
 * LSM modules.  The flags field is used to specify which fields within the
 * struct are valid and valid values can be created by bitwise OR'ing the
 * NETLBL_SECATTR_* defines.  The domain field is typically set by the LSM to
 * specify domain specific configuration settings and is not usually used by
 * NetLabel itself when returning security attributes to the LSM.
 *
 */
struct netlbl_lsm_secattr {
	u32 flags;
	/* bitmap values for 'flags' */
#define NETLBL_SECATTR_NONE             0x00000000
#define NETLBL_SECATTR_DOMAIN           0x00000001
#define NETLBL_SECATTR_DOMAIN_CPY       (NETLBL_SECATTR_DOMAIN | \
					 NETLBL_SECATTR_FREE_DOMAIN)
#define NETLBL_SECATTR_CACHE            0x00000002
#define NETLBL_SECATTR_MLS_LVL          0x00000004
#define NETLBL_SECATTR_MLS_CAT          0x00000008
#define NETLBL_SECATTR_SECID            0x00000010
	/* bitmap meta-values for 'flags' */
#define NETLBL_SECATTR_FREE_DOMAIN      0x01000000
#define NETLBL_SECATTR_CACHEABLE        (NETLBL_SECATTR_MLS_LVL | \
					 NETLBL_SECATTR_MLS_CAT | \
					 NETLBL_SECATTR_SECID)
	u32 type;
	char *domain;
	struct netlbl_lsm_cache *cache;
	union {
		struct {
			struct netlbl_lsm_secattr_catmap *cat;
			u32 lvl;
		} mls;
		u32 secid;
	} attr;
};

/*
 * LSM security attribute operations (inline)
 */

/**
 * netlbl_secattr_cache_alloc - Allocate and initialize a secattr cache
 * @flags: the memory allocation flags
 *
 * Description:
 * Allocate and initialize a netlbl_lsm_cache structure.  Returns a pointer
 * on success, NULL on failure.
 *
 */
static inline struct netlbl_lsm_cache *netlbl_secattr_cache_alloc(gfp_t flags)
{
	struct netlbl_lsm_cache *cache;

	cache = kzalloc(sizeof(*cache), flags);
	if (cache)
		atomic_set(&cache->refcount, 1);
	return cache;
}

/**
 * netlbl_secattr_cache_free - Frees a netlbl_lsm_cache struct
 * @cache: the struct to free
 *
 * Description:
 * Frees @secattr including all of the internal buffers.
 *
 */
static inline void netlbl_secattr_cache_free(struct netlbl_lsm_cache *cache)
{
	if (!atomic_dec_and_test(&cache->refcount))
		return;

	if (cache->free)
		cache->free(cache->data);
	kfree(cache);
}

/**
 * netlbl_secattr_catmap_alloc - Allocate a LSM secattr catmap
 * @flags: memory allocation flags
 *
 * Description:
 * Allocate memory for a LSM secattr catmap, returns a pointer on success, NULL
 * on failure.
 *
 */
static inline struct netlbl_lsm_secattr_catmap *netlbl_secattr_catmap_alloc(
	                                                           gfp_t flags)
{
	return kzalloc(sizeof(struct netlbl_lsm_secattr_catmap), flags);
}

/**
 * netlbl_secattr_catmap_free - Free a LSM secattr catmap
 * @catmap: the category bitmap
 *
 * Description:
 * Free a LSM secattr catmap.
 *
 */
static inline void netlbl_secattr_catmap_free(
	                              struct netlbl_lsm_secattr_catmap *catmap)
{
	struct netlbl_lsm_secattr_catmap *iter;

	do {
		iter = catmap;
		catmap = catmap->next;
		kfree(iter);
	} while (catmap);
}

/**
 * netlbl_secattr_init - Initialize a netlbl_lsm_secattr struct
 * @secattr: the struct to initialize
 *
 * Description:
 * Initialize an already allocated netlbl_lsm_secattr struct.
 *
 */
static inline void netlbl_secattr_init(struct netlbl_lsm_secattr *secattr)
{
	memset(secattr, 0, sizeof(*secattr));
}

/**
 * netlbl_secattr_destroy - Clears a netlbl_lsm_secattr struct
 * @secattr: the struct to clear
 *
 * Description:
 * Destroys the @secattr struct, including freeing all of the internal buffers.
 * The struct must be reset with a call to netlbl_secattr_init() before reuse.
 *
 */
static inline void netlbl_secattr_destroy(struct netlbl_lsm_secattr *secattr)
{
	if (secattr->flags & NETLBL_SECATTR_FREE_DOMAIN)
		kfree(secattr->domain);
	if (secattr->flags & NETLBL_SECATTR_CACHE)
		netlbl_secattr_cache_free(secattr->cache);
	if (secattr->flags & NETLBL_SECATTR_MLS_CAT)
		netlbl_secattr_catmap_free(secattr->attr.mls.cat);
}

/**
 * netlbl_secattr_alloc - Allocate and initialize a netlbl_lsm_secattr struct
 * @flags: the memory allocation flags
 *
 * Description:
 * Allocate and initialize a netlbl_lsm_secattr struct.  Returns a valid
 * pointer on success, or NULL on failure.
 *
 */
static inline struct netlbl_lsm_secattr *netlbl_secattr_alloc(gfp_t flags)
{
	return kzalloc(sizeof(struct netlbl_lsm_secattr), flags);
}

/**
 * netlbl_secattr_free - Frees a netlbl_lsm_secattr struct
 * @secattr: the struct to free
 *
 * Description:
 * Frees @secattr including all of the internal buffers.
 *
 */
static inline void netlbl_secattr_free(struct netlbl_lsm_secattr *secattr)
{
	netlbl_secattr_destroy(secattr);
	kfree(secattr);
}

#ifdef CONFIG_NETLABEL
/*
 * LSM configuration operations
 */
int netlbl_cfg_map_del(const char *domain, struct netlbl_audit *audit_info);
int netlbl_cfg_unlbl_add_map(const char *domain,
			     struct netlbl_audit *audit_info);
int netlbl_cfg_cipsov4_add(struct cipso_v4_doi *doi_def,
			   struct netlbl_audit *audit_info);
int netlbl_cfg_cipsov4_add_map(struct cipso_v4_doi *doi_def,
			       const char *domain,
			       struct netlbl_audit *audit_info);
int netlbl_cfg_cipsov4_del(u32 doi, struct netlbl_audit *audit_info);

/*
 * LSM security attribute operations
 */
int netlbl_secattr_catmap_walk(struct netlbl_lsm_secattr_catmap *catmap,
			       u32 offset);
int netlbl_secattr_catmap_walk_rng(struct netlbl_lsm_secattr_catmap *catmap,
				   u32 offset);
int netlbl_secattr_catmap_setbit(struct netlbl_lsm_secattr_catmap *catmap,
				 u32 bit,
				 gfp_t flags);
int netlbl_secattr_catmap_setrng(struct netlbl_lsm_secattr_catmap *catmap,
				 u32 start,
				 u32 end,
				 gfp_t flags);

/*
 * LSM protocol operations (NetLabel LSM/kernel API)
 */
int netlbl_enabled(void);
int netlbl_sock_setattr(struct sock *sk,
			const struct netlbl_lsm_secattr *secattr);
int netlbl_sock_getattr(struct sock *sk,
			struct netlbl_lsm_secattr *secattr);
int netlbl_skbuff_getattr(const struct sk_buff *skb,
			  u16 family,
			  struct netlbl_lsm_secattr *secattr);
void netlbl_skbuff_err(struct sk_buff *skb, int error);

/*
 * LSM label mapping cache operations
 */
void netlbl_cache_invalidate(void);
int netlbl_cache_add(const struct sk_buff *skb,
		     const struct netlbl_lsm_secattr *secattr);
#else
static inline int netlbl_cfg_map_del(const char *domain,
				     struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_unlbl_add_map(const char *domain,
					   struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_cipsov4_add(struct cipso_v4_doi *doi_def,
					 struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_cipsov4_add_map(struct cipso_v4_doi *doi_def,
					     const char *domain,
					     struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_cipsov4_del(u32 doi,
					 struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_secattr_catmap_walk(
	                              struct netlbl_lsm_secattr_catmap *catmap,
				      u32 offset)
{
	return -ENOENT;
}
static inline int netlbl_secattr_catmap_walk_rng(
				      struct netlbl_lsm_secattr_catmap *catmap,
				      u32 offset)
{
	return -ENOENT;
}
static inline int netlbl_secattr_catmap_setbit(
	                              struct netlbl_lsm_secattr_catmap *catmap,
				      u32 bit,
				      gfp_t flags)
{
	return 0;
}
static inline int netlbl_secattr_catmap_setrng(
	                              struct netlbl_lsm_secattr_catmap *catmap,
				      u32 start,
				      u32 end,
				      gfp_t flags)
{
	return 0;
}
static inline int netlbl_enabled(void)
{
	return 0;
}
static inline int netlbl_sock_setattr(struct sock *sk,
				     const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline int netlbl_sock_getattr(struct sock *sk,
				      struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline int netlbl_skbuff_getattr(const struct sk_buff *skb,
					u16 family,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline void netlbl_skbuff_err(struct sk_buff *skb, int error)
{
	return;
}
static inline void netlbl_cache_invalidate(void)
{
	return;
}
static inline int netlbl_cache_add(const struct sk_buff *skb,
				   const struct netlbl_lsm_secattr *secattr)
{
	return 0;
}
#endif /* CONFIG_NETLABEL */

#endif /* _NETLABEL_H */
