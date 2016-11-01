/*
 * CALIPSO - Common Architecture Label IPv6 Security Option
 *
 * This is an implementation of the CALIPSO protocol as specified in
 * RFC 5570.
 *
 * Authors: Paul Moore <paul@paul-moore.com>
 *          Huw Davies <huw@codeweavers.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
 * (c) Copyright Huw Davies <huw@codeweavers.com>, 2015
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
 * along with this program;  if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _CALIPSO_H
#define _CALIPSO_H

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <net/netlabel.h>
#include <net/request_sock.h>
#include <linux/atomic.h>
#include <asm/unaligned.h>

/* known doi values */
#define CALIPSO_DOI_UNKNOWN          0x00000000

/* doi mapping types */
#define CALIPSO_MAP_UNKNOWN          0
#define CALIPSO_MAP_PASS             2

/*
 * CALIPSO DOI definitions
 */

/* DOI definition struct */
struct calipso_doi {
	u32 doi;
	u32 type;

	atomic_t refcount;
	struct list_head list;
	struct rcu_head rcu;
};

/*
 * Sysctl Variables
 */
extern int calipso_cache_enabled;
extern int calipso_cache_bucketsize;

#ifdef CONFIG_NETLABEL
int __init calipso_init(void);
void calipso_exit(void);
bool calipso_validate(const struct sk_buff *skb, const unsigned char *option);
#else
static inline int __init calipso_init(void)
{
	return 0;
}

static inline void calipso_exit(void)
{
}
static inline bool calipso_validate(const struct sk_buff *skb,
				    const unsigned char *option)
{
	return true;
}
#endif /* CONFIG_NETLABEL */

#endif /* _CALIPSO_H */
