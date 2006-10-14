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

#define NETLBL_PROTO_VERSION            1

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
};

/* Domain mapping definition struct */
struct netlbl_dom_map;

/* Domain mapping operations */
int netlbl_domhsh_remove(const char *domain, struct netlbl_audit *audit_info);

/* LSM security attributes */
struct netlbl_lsm_cache {
	atomic_t refcount;
	void (*free) (const void *data);
	void *data;
};
struct netlbl_lsm_secattr {
	char *domain;

	u32 mls_lvl;
	u32 mls_lvl_vld;
	unsigned char *mls_cat;
	size_t mls_cat_len;

	struct netlbl_lsm_cache *cache;
};

/*
 * LSM security attribute operations
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
 * netlbl_secattr_init - Initialize a netlbl_lsm_secattr struct
 * @secattr: the struct to initialize
 *
 * Description:
 * Initialize an already allocated netlbl_lsm_secattr struct.  Returns zero on
 * success, negative values on error.
 *
 */
static inline int netlbl_secattr_init(struct netlbl_lsm_secattr *secattr)
{
	memset(secattr, 0, sizeof(*secattr));
	return 0;
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
	if (secattr->cache)
		netlbl_secattr_cache_free(secattr->cache);
	kfree(secattr->domain);
	kfree(secattr->mls_cat);
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
static inline struct netlbl_lsm_secattr *netlbl_secattr_alloc(int flags)
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

/*
 * LSM protocol operations
 */

#ifdef CONFIG_NETLABEL
int netlbl_socket_setattr(const struct socket *sock,
			  const struct netlbl_lsm_secattr *secattr);
int netlbl_sock_getattr(struct sock *sk,
			struct netlbl_lsm_secattr *secattr);
int netlbl_socket_getattr(const struct socket *sock,
			  struct netlbl_lsm_secattr *secattr);
int netlbl_skbuff_getattr(const struct sk_buff *skb,
			  struct netlbl_lsm_secattr *secattr);
void netlbl_skbuff_err(struct sk_buff *skb, int error);
#else
static inline int netlbl_socket_setattr(const struct socket *sock,
				     const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int netlbl_sock_getattr(struct sock *sk,
				      struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int netlbl_socket_getattr(const struct socket *sock,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int netlbl_skbuff_getattr(const struct sk_buff *skb,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline void netlbl_skbuff_err(struct sk_buff *skb, int error)
{
	return;
}
#endif /* CONFIG_NETLABEL */

/*
 * LSM label mapping cache operations
 */

#ifdef CONFIG_NETLABEL
void netlbl_cache_invalidate(void);
int netlbl_cache_add(const struct sk_buff *skb,
		     const struct netlbl_lsm_secattr *secattr);
#else
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
