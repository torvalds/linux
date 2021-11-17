/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NetLabel System
 *
 * The NetLabel system manages static and dynamic label mappings for network
 * protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006, 2008
 */

#ifndef _NETLABEL_H
#define _NETLABEL_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <net/netlink.h>
#include <net/request_sock.h>
#include <linux/refcount.h>

struct cipso_v4_doi;
struct calipso_doi;

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
 *  3: network selectors added to the NetLabel/LSM domain mapping and the
 *     CIPSO_V4_MAP_LOCAL CIPSO mapping was added
 */
#define NETLBL_PROTO_VERSION            3

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
#define NETLBL_NLTYPE_ADDRSELECT        6
#define NETLBL_NLTYPE_ADDRSELECT_NAME   "NLBL_ADRSEL"
#define NETLBL_NLTYPE_CALIPSO           7
#define NETLBL_NLTYPE_CALIPSO_NAME      "NLBL_CALIPSO"

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
	kuid_t loginuid;
	unsigned int sessionid;
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
	refcount_t refcount;
	void (*free) (const void *data);
	void *data;
};

/**
 * struct netlbl_lsm_catmap - NetLabel LSM secattr category bitmap
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
struct netlbl_lsm_catmap {
	u32 startbit;
	NETLBL_CATMAP_MAPTYPE bitmap[NETLBL_CATMAP_MAPCNT];
	struct netlbl_lsm_catmap *next;
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
	struct {
		struct {
			struct netlbl_lsm_catmap *cat;
			u32 lvl;
		} mls;
		u32 secid;
	} attr;
};

/**
 * struct netlbl_calipso_ops - NetLabel CALIPSO operations
 * @doi_add: add a CALIPSO DOI
 * @doi_free: free a CALIPSO DOI
 * @doi_getdef: returns a reference to a DOI
 * @doi_putdef: releases a reference of a DOI
 * @doi_walk: enumerate the DOI list
 * @sock_getattr: retrieve the socket's attr
 * @sock_setattr: set the socket's attr
 * @sock_delattr: remove the socket's attr
 * @req_setattr: set the req socket's attr
 * @req_delattr: remove the req socket's attr
 * @opt_getattr: retrieve attr from memory block
 * @skbuff_optptr: find option in packet
 * @skbuff_setattr: set the skbuff's attr
 * @skbuff_delattr: remove the skbuff's attr
 * @cache_invalidate: invalidate cache
 * @cache_add: add cache entry
 *
 * Description:
 * This structure is filled out by the CALIPSO engine and passed
 * to the NetLabel core via a call to netlbl_calipso_ops_register().
 * It enables the CALIPSO engine (and hence IPv6) to be compiled
 * as a module.
 */
struct netlbl_calipso_ops {
	int (*doi_add)(struct calipso_doi *doi_def,
		       struct netlbl_audit *audit_info);
	void (*doi_free)(struct calipso_doi *doi_def);
	int (*doi_remove)(u32 doi, struct netlbl_audit *audit_info);
	struct calipso_doi *(*doi_getdef)(u32 doi);
	void (*doi_putdef)(struct calipso_doi *doi_def);
	int (*doi_walk)(u32 *skip_cnt,
			int (*callback)(struct calipso_doi *doi_def, void *arg),
			void *cb_arg);
	int (*sock_getattr)(struct sock *sk,
			    struct netlbl_lsm_secattr *secattr);
	int (*sock_setattr)(struct sock *sk,
			    const struct calipso_doi *doi_def,
			    const struct netlbl_lsm_secattr *secattr);
	void (*sock_delattr)(struct sock *sk);
	int (*req_setattr)(struct request_sock *req,
			   const struct calipso_doi *doi_def,
			   const struct netlbl_lsm_secattr *secattr);
	void (*req_delattr)(struct request_sock *req);
	int (*opt_getattr)(const unsigned char *calipso,
			   struct netlbl_lsm_secattr *secattr);
	unsigned char *(*skbuff_optptr)(const struct sk_buff *skb);
	int (*skbuff_setattr)(struct sk_buff *skb,
			      const struct calipso_doi *doi_def,
			      const struct netlbl_lsm_secattr *secattr);
	int (*skbuff_delattr)(struct sk_buff *skb);
	void (*cache_invalidate)(void);
	int (*cache_add)(const unsigned char *calipso_ptr,
			 const struct netlbl_lsm_secattr *secattr);
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
		refcount_set(&cache->refcount, 1);
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
	if (!refcount_dec_and_test(&cache->refcount))
		return;

	if (cache->free)
		cache->free(cache->data);
	kfree(cache);
}

/**
 * netlbl_catmap_alloc - Allocate a LSM secattr catmap
 * @flags: memory allocation flags
 *
 * Description:
 * Allocate memory for a LSM secattr catmap, returns a pointer on success, NULL
 * on failure.
 *
 */
static inline struct netlbl_lsm_catmap *netlbl_catmap_alloc(gfp_t flags)
{
	return kzalloc(sizeof(struct netlbl_lsm_catmap), flags);
}

/**
 * netlbl_catmap_free - Free a LSM secattr catmap
 * @catmap: the category bitmap
 *
 * Description:
 * Free a LSM secattr catmap.
 *
 */
static inline void netlbl_catmap_free(struct netlbl_lsm_catmap *catmap)
{
	struct netlbl_lsm_catmap *iter;

	while (catmap) {
		iter = catmap;
		catmap = catmap->next;
		kfree(iter);
	}
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
		netlbl_catmap_free(secattr->attr.mls.cat);
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
int netlbl_cfg_map_del(const char *domain,
		       u16 family,
		       const void *addr,
		       const void *mask,
		       struct netlbl_audit *audit_info);
int netlbl_cfg_unlbl_map_add(const char *domain,
			     u16 family,
			     const void *addr,
			     const void *mask,
			     struct netlbl_audit *audit_info);
int netlbl_cfg_unlbl_static_add(struct net *net,
				const char *dev_name,
				const void *addr,
				const void *mask,
				u16 family,
				u32 secid,
				struct netlbl_audit *audit_info);
int netlbl_cfg_unlbl_static_del(struct net *net,
				const char *dev_name,
				const void *addr,
				const void *mask,
				u16 family,
				struct netlbl_audit *audit_info);
int netlbl_cfg_cipsov4_add(struct cipso_v4_doi *doi_def,
			   struct netlbl_audit *audit_info);
void netlbl_cfg_cipsov4_del(u32 doi, struct netlbl_audit *audit_info);
int netlbl_cfg_cipsov4_map_add(u32 doi,
			       const char *domain,
			       const struct in_addr *addr,
			       const struct in_addr *mask,
			       struct netlbl_audit *audit_info);
int netlbl_cfg_calipso_add(struct calipso_doi *doi_def,
			   struct netlbl_audit *audit_info);
void netlbl_cfg_calipso_del(u32 doi, struct netlbl_audit *audit_info);
int netlbl_cfg_calipso_map_add(u32 doi,
			       const char *domain,
			       const struct in6_addr *addr,
			       const struct in6_addr *mask,
			       struct netlbl_audit *audit_info);
/*
 * LSM security attribute operations
 */
int netlbl_catmap_walk(struct netlbl_lsm_catmap *catmap, u32 offset);
int netlbl_catmap_walkrng(struct netlbl_lsm_catmap *catmap, u32 offset);
int netlbl_catmap_getlong(struct netlbl_lsm_catmap *catmap,
			  u32 *offset,
			  unsigned long *bitmap);
int netlbl_catmap_setbit(struct netlbl_lsm_catmap **catmap,
			 u32 bit,
			 gfp_t flags);
int netlbl_catmap_setrng(struct netlbl_lsm_catmap **catmap,
			 u32 start,
			 u32 end,
			 gfp_t flags);
int netlbl_catmap_setlong(struct netlbl_lsm_catmap **catmap,
			  u32 offset,
			  unsigned long bitmap,
			  gfp_t flags);

/* Bitmap functions
 */
int netlbl_bitmap_walk(const unsigned char *bitmap, u32 bitmap_len,
		       u32 offset, u8 state);
void netlbl_bitmap_setbit(unsigned char *bitmap, u32 bit, u8 state);

/*
 * LSM protocol operations (NetLabel LSM/kernel API)
 */
int netlbl_enabled(void);
int netlbl_sock_setattr(struct sock *sk,
			u16 family,
			const struct netlbl_lsm_secattr *secattr);
void netlbl_sock_delattr(struct sock *sk);
int netlbl_sock_getattr(struct sock *sk,
			struct netlbl_lsm_secattr *secattr);
int netlbl_conn_setattr(struct sock *sk,
			struct sockaddr *addr,
			const struct netlbl_lsm_secattr *secattr);
int netlbl_req_setattr(struct request_sock *req,
		       const struct netlbl_lsm_secattr *secattr);
void netlbl_req_delattr(struct request_sock *req);
int netlbl_skbuff_setattr(struct sk_buff *skb,
			  u16 family,
			  const struct netlbl_lsm_secattr *secattr);
int netlbl_skbuff_getattr(const struct sk_buff *skb,
			  u16 family,
			  struct netlbl_lsm_secattr *secattr);
void netlbl_skbuff_err(struct sk_buff *skb, u16 family, int error, int gateway);

/*
 * LSM label mapping cache operations
 */
void netlbl_cache_invalidate(void);
int netlbl_cache_add(const struct sk_buff *skb, u16 family,
		     const struct netlbl_lsm_secattr *secattr);

/*
 * Protocol engine operations
 */
struct audit_buffer *netlbl_audit_start(int type,
					struct netlbl_audit *audit_info);
#else
static inline int netlbl_cfg_map_del(const char *domain,
				     u16 family,
				     const void *addr,
				     const void *mask,
				     struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_unlbl_map_add(const char *domain,
					   u16 family,
					   void *addr,
					   void *mask,
					   struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_unlbl_static_add(struct net *net,
					      const char *dev_name,
					      const void *addr,
					      const void *mask,
					      u16 family,
					      u32 secid,
					      struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_unlbl_static_del(struct net *net,
					      const char *dev_name,
					      const void *addr,
					      const void *mask,
					      u16 family,
					      struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_cipsov4_add(struct cipso_v4_doi *doi_def,
					 struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline void netlbl_cfg_cipsov4_del(u32 doi,
					  struct netlbl_audit *audit_info)
{
	return;
}
static inline int netlbl_cfg_cipsov4_map_add(u32 doi,
					     const char *domain,
					     const struct in_addr *addr,
					     const struct in_addr *mask,
					     struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_cfg_calipso_add(struct calipso_doi *doi_def,
					 struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline void netlbl_cfg_calipso_del(u32 doi,
					  struct netlbl_audit *audit_info)
{
	return;
}
static inline int netlbl_cfg_calipso_map_add(u32 doi,
					     const char *domain,
					     const struct in6_addr *addr,
					     const struct in6_addr *mask,
					     struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}
static inline int netlbl_catmap_walk(struct netlbl_lsm_catmap *catmap,
				     u32 offset)
{
	return -ENOENT;
}
static inline int netlbl_catmap_walkrng(struct netlbl_lsm_catmap *catmap,
					u32 offset)
{
	return -ENOENT;
}
static inline int netlbl_catmap_getlong(struct netlbl_lsm_catmap *catmap,
					u32 *offset,
					unsigned long *bitmap)
{
	return 0;
}
static inline int netlbl_catmap_setbit(struct netlbl_lsm_catmap **catmap,
				       u32 bit,
				       gfp_t flags)
{
	return 0;
}
static inline int netlbl_catmap_setrng(struct netlbl_lsm_catmap **catmap,
				       u32 start,
				       u32 end,
				       gfp_t flags)
{
	return 0;
}
static inline int netlbl_catmap_setlong(struct netlbl_lsm_catmap **catmap,
					u32 offset,
					unsigned long bitmap,
					gfp_t flags)
{
	return 0;
}
static inline int netlbl_enabled(void)
{
	return 0;
}
static inline int netlbl_sock_setattr(struct sock *sk,
				      u16 family,
				      const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline void netlbl_sock_delattr(struct sock *sk)
{
}
static inline int netlbl_sock_getattr(struct sock *sk,
				      struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline int netlbl_conn_setattr(struct sock *sk,
				      struct sockaddr *addr,
				      const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline int netlbl_req_setattr(struct request_sock *req,
				     const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline void netlbl_req_delattr(struct request_sock *req)
{
	return;
}
static inline int netlbl_skbuff_setattr(struct sk_buff *skb,
				      u16 family,
				      const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline int netlbl_skbuff_getattr(const struct sk_buff *skb,
					u16 family,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}
static inline void netlbl_skbuff_err(struct sk_buff *skb,
				     int error,
				     int gateway)
{
	return;
}
static inline void netlbl_cache_invalidate(void)
{
	return;
}
static inline int netlbl_cache_add(const struct sk_buff *skb, u16 family,
				   const struct netlbl_lsm_secattr *secattr)
{
	return 0;
}
static inline struct audit_buffer *netlbl_audit_start(int type,
						struct netlbl_audit *audit_info)
{
	return NULL;
}
#endif /* CONFIG_NETLABEL */

const struct netlbl_calipso_ops *
netlbl_calipso_ops_register(const struct netlbl_calipso_ops *ops);

#endif /* _NETLABEL_H */
