/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CIPSO - Commercial IP Security Option
 *
 * This is an implementation of the CIPSO 2.2 protocol as specified in
 * draft-ietf-cipso-ipsecurity-01.txt with additional tag types as found in
 * FIPS-188, copies of both documents can be found in the Documentation
 * directory.  While CIPSO never became a full IETF RFC standard many vendors
 * have chosen to adopt the protocol and over the years it has become a
 * de-facto standard for labeled networking.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 */

#ifndef _CIPSO_IPV4_H
#define _CIPSO_IPV4_H

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <net/netlabel.h>
#include <net/request_sock.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/unaligned.h>

/* known doi values */
#define CIPSO_V4_DOI_UNKNOWN          0x00000000

/* standard tag types */
#define CIPSO_V4_TAG_INVALID          0
#define CIPSO_V4_TAG_RBITMAP          1
#define CIPSO_V4_TAG_ENUM             2
#define CIPSO_V4_TAG_RANGE            5
#define CIPSO_V4_TAG_PBITMAP          6
#define CIPSO_V4_TAG_FREEFORM         7

/* non-standard tag types (tags > 127) */
#define CIPSO_V4_TAG_LOCAL            128

/* doi mapping types */
#define CIPSO_V4_MAP_UNKNOWN          0
#define CIPSO_V4_MAP_TRANS            1
#define CIPSO_V4_MAP_PASS             2
#define CIPSO_V4_MAP_LOCAL            3

/* limits */
#define CIPSO_V4_MAX_REM_LVLS         255
#define CIPSO_V4_INV_LVL              0x80000000
#define CIPSO_V4_MAX_LOC_LVLS         (CIPSO_V4_INV_LVL - 1)
#define CIPSO_V4_MAX_REM_CATS         65534
#define CIPSO_V4_INV_CAT              0x80000000
#define CIPSO_V4_MAX_LOC_CATS         (CIPSO_V4_INV_CAT - 1)

/*
 * CIPSO DOI definitions
 */

/* DOI definition struct */
#define CIPSO_V4_TAG_MAXCNT           5
struct cipso_v4_doi {
	u32 doi;
	u32 type;
	union {
		struct cipso_v4_std_map_tbl *std;
	} map;
	u8 tags[CIPSO_V4_TAG_MAXCNT];

	refcount_t refcount;
	struct list_head list;
	struct rcu_head rcu;
};

/* Standard CIPSO mapping table */
/* NOTE: the highest order bit (i.e. 0x80000000) is an 'invalid' flag, if the
 *       bit is set then consider that value as unspecified, meaning the
 *       mapping for that particular level/category is invalid */
struct cipso_v4_std_map_tbl {
	struct {
		u32 *cipso;
		u32 *local;
		u32 cipso_size;
		u32 local_size;
	} lvl;
	struct {
		u32 *cipso;
		u32 *local;
		u32 cipso_size;
		u32 local_size;
	} cat;
};

/*
 * Sysctl Variables
 */

#ifdef CONFIG_NETLABEL
extern int cipso_v4_cache_enabled;
extern int cipso_v4_cache_bucketsize;
extern int cipso_v4_rbm_optfmt;
extern int cipso_v4_rbm_strictvalid;
#endif

/*
 * DOI List Functions
 */

#ifdef CONFIG_NETLABEL
int cipso_v4_doi_add(struct cipso_v4_doi *doi_def,
		     struct netlbl_audit *audit_info);
void cipso_v4_doi_free(struct cipso_v4_doi *doi_def);
int cipso_v4_doi_remove(u32 doi, struct netlbl_audit *audit_info);
struct cipso_v4_doi *cipso_v4_doi_getdef(u32 doi);
void cipso_v4_doi_putdef(struct cipso_v4_doi *doi_def);
int cipso_v4_doi_walk(u32 *skip_cnt,
		     int (*callback) (struct cipso_v4_doi *doi_def, void *arg),
	             void *cb_arg);
#else
static inline int cipso_v4_doi_add(struct cipso_v4_doi *doi_def,
				   struct netlbl_audit *audit_info)
{
	return -ENOSYS;
}

static inline void cipso_v4_doi_free(struct cipso_v4_doi *doi_def)
{
	return;
}

static inline int cipso_v4_doi_remove(u32 doi,
				      struct netlbl_audit *audit_info)
{
	return 0;
}

static inline struct cipso_v4_doi *cipso_v4_doi_getdef(u32 doi)
{
	return NULL;
}

static inline int cipso_v4_doi_walk(u32 *skip_cnt,
		     int (*callback) (struct cipso_v4_doi *doi_def, void *arg),
		     void *cb_arg)
{
	return 0;
}
#endif /* CONFIG_NETLABEL */

/*
 * Label Mapping Cache Functions
 */

#ifdef CONFIG_NETLABEL
void cipso_v4_cache_invalidate(void);
int cipso_v4_cache_add(const unsigned char *cipso_ptr,
		       const struct netlbl_lsm_secattr *secattr);
#else
static inline void cipso_v4_cache_invalidate(void)
{
	return;
}

static inline int cipso_v4_cache_add(const unsigned char *cipso_ptr,
				     const struct netlbl_lsm_secattr *secattr)
{
	return 0;
}
#endif /* CONFIG_NETLABEL */

/*
 * Protocol Handling Functions
 */

#ifdef CONFIG_NETLABEL
void cipso_v4_error(struct sk_buff *skb, int error, u32 gateway);
int cipso_v4_getattr(const unsigned char *cipso,
		     struct netlbl_lsm_secattr *secattr);
int cipso_v4_sock_setattr(struct sock *sk,
			  const struct cipso_v4_doi *doi_def,
			  const struct netlbl_lsm_secattr *secattr,
			  bool sk_locked);
void cipso_v4_sock_delattr(struct sock *sk);
int cipso_v4_sock_getattr(struct sock *sk, struct netlbl_lsm_secattr *secattr);
int cipso_v4_req_setattr(struct request_sock *req,
			 const struct cipso_v4_doi *doi_def,
			 const struct netlbl_lsm_secattr *secattr);
void cipso_v4_req_delattr(struct request_sock *req);
int cipso_v4_skbuff_setattr(struct sk_buff *skb,
			    const struct cipso_v4_doi *doi_def,
			    const struct netlbl_lsm_secattr *secattr);
int cipso_v4_skbuff_delattr(struct sk_buff *skb);
int cipso_v4_skbuff_getattr(const struct sk_buff *skb,
			    struct netlbl_lsm_secattr *secattr);
unsigned char *cipso_v4_optptr(const struct sk_buff *skb);
int cipso_v4_validate(const struct sk_buff *skb, unsigned char **option);
#else
static inline void cipso_v4_error(struct sk_buff *skb,
				  int error,
				  u32 gateway)
{
	return;
}

static inline int cipso_v4_getattr(const unsigned char *cipso,
				   struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int cipso_v4_sock_setattr(struct sock *sk,
				      const struct cipso_v4_doi *doi_def,
				      const struct netlbl_lsm_secattr *secattr,
				      bool sk_locked)
{
	return -ENOSYS;
}

static inline void cipso_v4_sock_delattr(struct sock *sk)
{
}

static inline int cipso_v4_sock_getattr(struct sock *sk,
					struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int cipso_v4_req_setattr(struct request_sock *req,
				       const struct cipso_v4_doi *doi_def,
				       const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline void cipso_v4_req_delattr(struct request_sock *req)
{
	return;
}

static inline int cipso_v4_skbuff_setattr(struct sk_buff *skb,
				      const struct cipso_v4_doi *doi_def,
				      const struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline int cipso_v4_skbuff_delattr(struct sk_buff *skb)
{
	return -ENOSYS;
}

static inline int cipso_v4_skbuff_getattr(const struct sk_buff *skb,
					  struct netlbl_lsm_secattr *secattr)
{
	return -ENOSYS;
}

static inline unsigned char *cipso_v4_optptr(const struct sk_buff *skb)
{
	return NULL;
}

static inline int cipso_v4_validate(const struct sk_buff *skb,
				    unsigned char **option)
{
	unsigned char *opt = *option;
	unsigned char err_offset = 0;
	u8 opt_len = opt[1];
	u8 opt_iter;
	u8 tag_len;

	if (opt_len < 8) {
		err_offset = 1;
		goto out;
	}

	if (get_unaligned_be32(&opt[2]) == 0) {
		err_offset = 2;
		goto out;
	}

	for (opt_iter = 6; opt_iter < opt_len;) {
		if (opt_iter + 1 == opt_len) {
			err_offset = opt_iter;
			goto out;
		}
		tag_len = opt[opt_iter + 1];
		if ((tag_len == 0) || (tag_len > (opt_len - opt_iter))) {
			err_offset = opt_iter + 1;
			goto out;
		}
		opt_iter += tag_len;
	}

out:
	*option = opt + err_offset;
	return err_offset;

}
#endif /* CONFIG_NETLABEL */

#endif /* _CIPSO_IPV4_H */
